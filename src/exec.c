/*
 * Glances Embedded Linux Collector
 *
 * SPDX-FileCopyrightText: 2026 Nicolas Hennion <nicolas@nicolargo.com>
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Generic command execution collector module.
 */

#include "sysinfo.h"
#include "protocol.h"
#include "json_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

#define EXEC_MAX_STDOUT (1024 * 1024)  /* 1 MB limit for command stdout */
#define EXEC_MAX_STDERR (64 * 1024)   /* 64 KB limit for command stderr */

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

int collect_exec(json_writer_t *jw, const char *target_id, const char *cmd) {
    if (!cmd || strlen(cmd) == 0) {
        double now = get_timestamp_now();
        jw_write_envelope_error(jw, OP_EXEC, target_id, now,
                                ERR_INVALID_REQUEST, "Command argument cannot be empty", NULL, 0);
        return 1;
    }

    int out_pipe[2];
    int err_pipe[2];

    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        double now = get_timestamp_now();
        jw_write_envelope_error(jw, OP_EXEC, target_id, now,
                                ERR_INTERNAL_ERROR, "Failed to create pipes for command execution", NULL, 0);
        return 1;
    }

    struct timespec ts_start, ts_end;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);

        double now = get_timestamp_now();
        jw_write_envelope_error(jw, OP_EXEC, target_id, now,
                                ERR_INTERNAL_ERROR, "Failed to fork process for command execution", NULL, 0);
        return 1;
    }

    if (pid == 0) {
        /* Child process */
        close(out_pipe[0]);
        close(err_pipe[0]);

        if (dup2(out_pipe[1], STDOUT_FILENO) == -1 || dup2(err_pipe[1], STDERR_FILENO) == -1) {
            _exit(127);
        }

        close(out_pipe[1]);
        close(err_pipe[1]);

        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }

    /* Parent process */
    close(out_pipe[1]);
    close(err_pipe[1]);

    set_nonblocking(out_pipe[0]);
    set_nonblocking(err_pipe[0]);

    char *stdout_buf = (char *)malloc(EXEC_MAX_STDOUT + 1);
    char *stderr_buf = (char *)malloc(EXEC_MAX_STDERR + 1);
    if (!stdout_buf || !stderr_buf) {
        free(stdout_buf);
        free(stderr_buf);
        close(out_pipe[0]);
        close(err_pipe[0]);
        waitpid(pid, NULL, 0);

        double now = get_timestamp_now();
        jw_write_envelope_error(jw, OP_EXEC, target_id, now,
                                ERR_INTERNAL_ERROR, "Failed to allocate memory for command output buffers", NULL, 0);
        return 1;
    }

    size_t out_len = 0;
    size_t err_len = 0;
    int out_open = 1;
    int err_open = 1;

    while (out_open || err_open) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int max_fd = -1;

        if (out_open) {
            FD_SET(out_pipe[0], &rfds);
            if (out_pipe[0] > max_fd) max_fd = out_pipe[0];
        }
        if (err_open) {
            FD_SET(err_pipe[0], &rfds);
            if (err_pipe[0] > max_fd) max_fd = err_pipe[0];
        }

        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        int sel = select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (sel <= 0 && errno != EINTR) {
            /* Timeout or error */
            break;
        }

        if (out_open && FD_ISSET(out_pipe[0], &rfds)) {
            ssize_t n = read(out_pipe[0], stdout_buf + out_len, EXEC_MAX_STDOUT - out_len);
            if (n > 0) {
                out_len += (size_t)n;
                if (out_len >= EXEC_MAX_STDOUT) {
                    out_open = 0;
                }
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                out_open = 0;
            }
        }

        if (err_open && FD_ISSET(err_pipe[0], &rfds)) {
            ssize_t n = read(err_pipe[0], stderr_buf + err_len, EXEC_MAX_STDERR - err_len);
            if (n > 0) {
                err_len += (size_t)n;
                if (err_len >= EXEC_MAX_STDERR) {
                    err_open = 0;
                }
            } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                err_open = 0;
            }
        }
    }

    close(out_pipe[0]);
    close(err_pipe[0]);

    stdout_buf[out_len] = '\0';
    stderr_buf[err_len] = '\0';

    int status = 0;
    waitpid(pid, &status, 0);

    clock_gettime(CLOCK_MONOTONIC, &ts_end);
    double duration_ms = (ts_end.tv_sec - ts_start.tv_sec) * 1000.0 +
                         (ts_end.tv_nsec - ts_start.tv_nsec) / 1000000.0;
    if (duration_ms < 0.0) duration_ms = 0.0;

    int exit_code = -1;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    }

    double now = get_timestamp_now();
    jw_start_envelope_ok(jw, OP_EXEC, target_id, now);

    jw_string(jw, "command", cmd);
    jw_int(jw, "exit_code", exit_code);
    jw_string(jw, "stdout", stdout_buf);
    jw_string(jw, "stderr", stderr_buf);
    jw_double(jw, "duration_ms", duration_ms, 2);

    jw_end_envelope_ok(jw);

    free(stdout_buf);
    free(stderr_buf);
    return 0;
}
