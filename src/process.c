/*
 * Glances Embedded Linux Collector
 *
 * SPDX-FileCopyrightText: 2026 Nicolas Hennion <nicolas@nicolargo.com>
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Process list and process details collection implementation.
 */

#include "sysinfo.h"
#include "protocol.h"
#include "json_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <unistd.h>
#include <inttypes.h>
#include <errno.h>

typedef struct {
    int pid;
    char comm[64];
    char state[2];
    int ppid;
    long nice;
    long num_threads;
    uint64_t vsize;
    int64_t rss;
    int cpu_num;
    uid_t uid;
    char username[32];
    double mem_pct;
    double cpu_pct;
    char cmdline[MAX_CMDLINE_LEN];
    size_t cmdline_len;
} proc_summary_t;

static int is_numeric_dir(const char *name) {
    if (!name || *name == '\0') return 0;
    while (*name) {
        if (!isdigit((unsigned char)*name)) return 0;
        name++;
    }
    return 1;
}

static int parse_proc_stat(int pid, proc_summary_t *p) {
    char path[MAX_PATH_BUFFER];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[MAX_LINE_BUFFER];
    if (!fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* Locate first '(' and last ')' */
    char *open_p = strchr(line, '(');
    char *close_p = strrchr(line, ')');
    if (!open_p || !close_p || close_p <= open_p) {
        return -1;
    }

    p->pid = pid;

    /* Extract comm */
    size_t comm_len = (size_t)(close_p - (open_p + 1));
    if (comm_len >= sizeof(p->comm)) comm_len = sizeof(p->comm) - 1;
    memcpy(p->comm, open_p + 1, comm_len);
    p->comm[comm_len] = '\0';

    /* Fields after ')' are space-separated */
    char *rest = close_p + 2;
    char state_ch = 'S';
    int ppid = 0;
    long nice = 0;
    long threads = 1;
    unsigned long vsize = 0;
    long rss_pages = 0;
    int cpu_num = 0;

    int matched = sscanf(rest,
        "%c %d "                 /* 3: state, 4: ppid */
        "%*d %*d %*d %*d %*u "   /* 5-9: pgrp, session, tty_nr, tpgid, flags */
        "%*u %*u %*u %*u "       /* 10-13: minflt, cminflt, majflt, cmajflt */
        "%*u %*u %*d %*d "       /* 14-17: utime, stime, cutime, cstime */
        "%*d %ld %ld %*d %*u "   /* 18-22: priority, nice, num_threads, itrealval, starttime */
        "%lu %ld "               /* 23: vsize, 24: rss */
        "%*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u %*u " /* 25-38 */
        "%d",                    /* 39: processor (cpu_num) */
        &state_ch, &ppid,
        &nice, &threads,
        &vsize, &rss_pages,
        &cpu_num);

    p->state[0] = state_ch;
    p->state[1] = '\0';
    p->ppid = ppid;
    p->nice = nice;
    p->num_threads = (threads > 0) ? threads : 1;
    p->vsize = (uint64_t)vsize;
    p->rss = (int64_t)rss_pages;
    p->cpu_num = (matched >= 7) ? cpu_num : 0;

    return 0;
}

static void read_proc_status_uid(int pid, uid_t *uid) {
    char path[MAX_PATH_BUFFER];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);

    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[MAX_LINE_BUFFER];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "Uid:", 4) == 0) {
            unsigned int real_uid = 0;
            if (sscanf(line + 4, "%u", &real_uid) == 1) {
                *uid = (uid_t)real_uid;
            }
            break;
        }
    }
    fclose(fp);
}

static void read_proc_cmdline(int pid, char *dst, size_t dst_size, size_t *out_len) {
    char path[MAX_PATH_BUFFER];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        *out_len = 0;
        if (dst_size > 0) dst[0] = '\0';
        return;
    }

    size_t n = fread(dst, 1, dst_size - 1, fp);
    fclose(fp);
    dst[n] = '\0';
    *out_len = n;
}

static void write_cmdline_array(json_writer_t *jw, const char *key, const char *cmdline, size_t cmdline_len, const char *fallback_name) {
    if (key) {
        jw_key_start_array(jw, key);
    } else {
        jw_arr_start_array(jw);
    }

    if (cmdline_len == 0 || !cmdline || cmdline[0] == '\0') {
        jw_arr_string(jw, fallback_name ? fallback_name : "");
        jw_end_array(jw);
        return;
    }

    size_t pos = 0;
    int token_count = 0;
    while (pos < cmdline_len && token_count < 32) {
        const char *token = cmdline + pos;
        size_t tlen = strlen(token);
        if (tlen > 0) {
            jw_arr_string(jw, token);
            token_count++;
        }
        pos += tlen + 1;
    }

    if (token_count == 0) {
        jw_arr_string(jw, fallback_name ? fallback_name : "");
    }
    jw_end_array(jw);
}

int collect_process_list(json_writer_t *jw, const char *target_id) {
    double now = get_timestamp_now();
    jw_start_envelope_ok(jw, OP_PROCESS_LIST, target_id, now);

    uint64_t mem_total = 0;
    get_memory_info(&mem_total, NULL, NULL, NULL, NULL);
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;

    DIR *dir = opendir("/proc");
    if (!dir) {
        jw_key_start_array(jw, "processes");
        jw_end_array(jw);
        jw_end_envelope_ok(jw);
        return 0;
    }

    jw_key_start_array(jw, "processes");

    struct dirent *ent;
    int count = 0;
    while ((ent = readdir(dir)) != NULL && count < MAX_PROCS_LIST) {
        if (!is_numeric_dir(ent->d_name)) continue;

        int pid = atoi(ent->d_name);
        if (pid <= 0) continue;

        proc_summary_t p;
        memset(&p, 0, sizeof(p));
        if (parse_proc_stat(pid, &p) != 0) {
            continue; /* Process exited while iterating */
        }

        read_proc_status_uid(pid, &p.uid);
        resolve_username(p.uid, p.username, sizeof(p.username));

        read_proc_cmdline(pid, p.cmdline, sizeof(p.cmdline), &p.cmdline_len);

        int64_t rss_bytes = p.rss * page_size;
        if (rss_bytes < 0) rss_bytes = 0;
        double mem_pct = (mem_total > 0) ? (((double)rss_bytes / (double)mem_total) * 100.0) : 0.0;

        jw_arr_start_object(jw);
        jw_int(jw, "pid", p.pid);
        jw_string(jw, "name", p.comm);
        jw_string(jw, "username", p.username);
        jw_string(jw, "status", p.state);
        jw_int(jw, "nice", p.nice);
        jw_int(jw, "num_threads", p.num_threads);
        jw_double(jw, "cpu_percent", 0.0, 1);
        jw_double(jw, "memory_percent", mem_pct, 1);
        write_cmdline_array(jw, "cmdline", p.cmdline, p.cmdline_len, p.comm);
        jw_int(jw, "cpu_num", p.cpu_num);
        jw_end_object(jw);

        count++;
    }
    closedir(dir);

    jw_end_array(jw); /* processes */
    jw_end_envelope_ok(jw);
    return 0;
}

int collect_process_details(json_writer_t *jw, const char *target_id, int pid) {
    double now = get_timestamp_now();

    char proc_path[MAX_PATH_BUFFER];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d", pid);

    if (access(proc_path, F_OK) != 0) {
        /* Process not found or disappeared */
        jw_write_envelope_error(jw, OP_PROCESS_DETAILS, target_id, now,
                                ERR_PROCESS_NOT_FOUND, "Process not found or exited", "pid", pid);
        return 0;
    }

    proc_summary_t p;
    memset(&p, 0, sizeof(p));
    if (parse_proc_stat(pid, &p) != 0) {
        /* Disappeared between access check and stat open */
        jw_write_envelope_error(jw, OP_PROCESS_DETAILS, target_id, now,
                                ERR_PROCESS_NOT_FOUND, "Process exited before state could be read", "pid", pid);
        return 0;
    }

    read_proc_status_uid(pid, &p.uid);
    resolve_username(p.uid, p.username, sizeof(p.username));
    read_proc_cmdline(pid, p.cmdline, sizeof(p.cmdline), &p.cmdline_len);

    uint64_t mem_total = 0;
    get_memory_info(&mem_total, NULL, NULL, NULL, NULL);
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) page_size = 4096;

    int64_t rss_bytes = p.rss * page_size;
    if (rss_bytes < 0) rss_bytes = 0;
    double mem_pct = (mem_total > 0) ? (((double)rss_bytes / (double)mem_total) * 100.0) : 0.0;

    /* Context switches from /proc/<pid>/status */
    int64_t voluntary_ctxt = -1;
    int64_t nonvoluntary_ctxt = -1;
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/status", pid);
    FILE *fp_status = fopen(proc_path, "r");
    if (fp_status) {
        char sline[MAX_LINE_BUFFER];
        while (fgets(sline, sizeof(sline), fp_status)) {
            int64_t val = 0;
            if (sscanf(sline, "voluntary_ctxt_switches: %" SCNd64, &val) == 1) {
                voluntary_ctxt = val;
            } else if (sscanf(sline, "nonvoluntary_ctxt_switches: %" SCNd64, &val) == 1) {
                nonvoluntary_ctxt = val;
            }
        }
        fclose(fp_status);
    }

    /* IO counters from /proc/<pid>/io */
    bool has_io = false;
    uint64_t read_bytes = 0;
    uint64_t write_bytes = 0;
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/io", pid);
    FILE *fp_io = fopen(proc_path, "r");
    if (fp_io) {
        char ioline[MAX_LINE_BUFFER];
        while (fgets(ioline, sizeof(ioline), fp_io)) {
            uint64_t bytes = 0;
            if (sscanf(ioline, "read_bytes: %" SCNu64, &bytes) == 1) {
                read_bytes = bytes;
                has_io = true;
            } else if (sscanf(ioline, "write_bytes: %" SCNu64, &bytes) == 1) {
                write_bytes = bytes;
                has_io = true;
            }
        }
        fclose(fp_io);
    }

    /* Open file descriptors from /proc/<pid>/fd */
    int num_fds = -1;
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/fd", pid);
    DIR *fd_dir = opendir(proc_path);
    if (fd_dir) {
        num_fds = 0;
        struct dirent *de;
        while ((de = readdir(fd_dir)) != NULL) {
            if (de->d_name[0] != '.') num_fds++;
        }
        closedir(fd_dir);
    }

    /* Build OK response */
    jw_start_envelope_ok(jw, OP_PROCESS_DETAILS, target_id, now);

    jw_int(jw, "pid", p.pid);
    jw_string(jw, "name", p.comm);
    write_cmdline_array(jw, "cmdline", p.cmdline, p.cmdline_len, p.comm);
    jw_string(jw, "username", p.username);
    jw_string(jw, "status", p.state);
    jw_int(jw, "ppid", p.ppid);
    jw_int(jw, "num_threads", p.num_threads);
    jw_double(jw, "cpu_percent", 0.0, 1);
    jw_double(jw, "memory_percent", mem_pct, 1);
    jw_int(jw, "cpu_num", p.cpu_num);

    /* CPU Affinity array */
    int cores = get_core_count();
    jw_key_start_array(jw, "cpu_affinity");
    for (int i = 0; i < cores; i++) {
        jw_arr_int(jw, i);
    }
    jw_end_array(jw);

    jw_double(jw, "cpu_min", 0.0, 1);
    jw_double(jw, "cpu_max", 0.0, 1);
    jw_double(jw, "cpu_mean", 0.0, 1);

    jw_int(jw, "memory_min", rss_bytes);
    jw_int(jw, "memory_max", rss_bytes);
    jw_int(jw, "memory_mean", rss_bytes);

    jw_key_start_object(jw, "memory_info");
    jw_int(jw, "rss", rss_bytes);
    jw_uint64(jw, "vms", p.vsize);
    jw_end_object(jw); /* memory_info */

    jw_int(jw, "memory_swap", 0);

    /* Distinction between unsupported and measured 0 */
    if (num_fds >= 0) {
        jw_int(jw, "num_fds", num_fds);
    } else {
        jw_null(jw, "num_fds"); /* permission denied */
    }

    jw_null(jw, "num_handles"); /* Unsupported on Linux */
    jw_null(jw, "tcp");
    jw_null(jw, "udp");

    if (voluntary_ctxt >= 0 && nonvoluntary_ctxt >= 0) {
        jw_key_start_object(jw, "num_ctx_switches");
        jw_int(jw, "voluntary", voluntary_ctxt);
        jw_int(jw, "involuntary", nonvoluntary_ctxt);
        jw_end_object(jw);
    } else {
        jw_null(jw, "num_ctx_switches");
    }

    if (has_io) {
        jw_key_start_object(jw, "io_counters");
        jw_uint64(jw, "read_bytes", read_bytes);
        jw_uint64(jw, "write_bytes", write_bytes);
        jw_end_object(jw);
    } else {
        jw_null(jw, "io_counters"); /* Permission denied or kernel lacks IO accounting */
    }

    jw_null(jw, "ionice");

    jw_end_envelope_ok(jw);
    return 0;
}
