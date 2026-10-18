/*
 * Glances Embedded Linux Collector
 *
 * SPDX-FileCopyrightText: 2026 Nicolas Hennion <nicolas@nicolargo.com>
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Main entry point for standalone C collector.
 */

#include "protocol.h"
#include "json_writer.h"
#include "sysinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

static void print_usage(const char *prog_name) {
    fprintf(stderr,
            "Glances Embedded Linux Collector v%s (Protocol v%s)\n\n"
            "Usage: %s --op <operation> [options]\n\n"
            "Operations:\n"
            "  capabilities      Report collector capabilities and target architecture\n"
            "  summary           System summary (OS, uptime, CPU, memory, load)\n"
            "  process_list      List running processes with basic metrics\n"
            "  process_details   Detailed metrics for a specific PID (requires --pid)\n"
            "  exec              Execute command and return stdout/stderr/code (requires --cmd)\n\n"
            "Options:\n"
            "  -o, --op <name>       Operation to perform (required)\n"
            "  -c, --cmd <command>   Command to execute (required for exec)\n"
            "  -p, --pid <pid>       Target PID (required for process_details)\n"
            "  -t, --target <id>     Target identity string (default: hostname)\n"
            "  -f, --format <fmt>    Output format (must be 'json', default: json)\n"
            "  -v, --version         Show collector version\n"
            "  -h, --help            Show this help text\n",
            COLLECTOR_VERSION, PROTOCOL_VERSION, prog_name);
}

int main(int argc, char *argv[]) {
    char *op = NULL;
    char *cmd = NULL;
    char *target_id = NULL;
    int pid = -1;

    static struct option long_options[] = {
        {"op",      required_argument, 0, 'o'},
        {"cmd",     required_argument, 0, 'c'},
        {"pid",     required_argument, 0, 'p'},
        {"target",  required_argument, 0, 't'},
        {"format",  required_argument, 0, 'f'},
        {"version", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "o:c:p:t:f:vh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'o':
                op = optarg;
                break;
            case 'c':
                cmd = optarg;
                break;
            case 'p':
                pid = atoi(optarg);
                break;
            case 't':
                target_id = optarg;
                break;
            case 'f':
                if (strcmp(optarg, "json") != 0) {
                    fprintf(stderr, "[collector] Warning: Only 'json' format is supported. Using json.\n");
                }
                break;
            case 'v':
                fprintf(stderr, "glances-collector v%s (protocol %s)\n", COLLECTOR_VERSION, PROTOCOL_VERSION);
                return 0;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    char default_hostname[256];
    if (!target_id) {
        if (gethostname(default_hostname, sizeof(default_hostname)) == 0) {
            target_id = default_hostname;
        } else {
            target_id = "embedded-target";
        }
    }

    char *out_buf = (char *)malloc(MAX_OUTPUT_BUFFER);
    if (!out_buf) {
        fprintf(stderr, "[collector] Fatal: failed to allocate %d bytes output buffer\n", MAX_OUTPUT_BUFFER);
        return 1;
    }

    json_writer_t jw;
    jw_init(&jw, out_buf, MAX_OUTPUT_BUFFER);

    double now = get_timestamp_now();

    if (!op) {
        fprintf(stderr, "[collector] Error: --op <operation> is required\n");
        jw_write_envelope_error(&jw, "unknown", target_id, now,
                                ERR_INVALID_REQUEST, "Missing required --op argument", NULL, 0);
        fwrite(jw_get_buf(&jw), 1, jw_len(&jw), stdout);
        putchar('\n');
        fflush(stdout);
        free(out_buf);
        return 1;
    }

    int ret = 0;
    if (strcmp(op, OP_CAPABILITIES) == 0) {
        ret = collect_capabilities(&jw, target_id);
    } else if (strcmp(op, OP_SUMMARY) == 0) {
        ret = collect_summary(&jw, target_id);
    } else if (strcmp(op, OP_PROCESS_LIST) == 0) {
        ret = collect_process_list(&jw, target_id);
    } else if (strcmp(op, OP_PROCESS_DETAILS) == 0) {
        if (pid <= 0) {
            fprintf(stderr, "[collector] Error: --pid <positive-integer> is required for process_details\n");
            jw_write_envelope_error(&jw, op, target_id, now,
                                    ERR_INVALID_REQUEST, "Missing or invalid --pid argument for process_details", NULL, 0);
            ret = 1;
        } else {
            ret = collect_process_details(&jw, target_id, pid);
        }
    } else if (strcmp(op, OP_EXEC) == 0) {
        if (!cmd) {
            fprintf(stderr, "[collector] Error: --cmd <command> is required for exec\n");
            jw_write_envelope_error(&jw, op, target_id, now,
                                    ERR_INVALID_REQUEST, "Missing required --cmd argument for exec", NULL, 0);
            ret = 1;
        } else {
            ret = collect_exec(&jw, target_id, cmd);
        }
    } else {
        fprintf(stderr, "[collector] Error: Unsupported operation '%s'\n", op);
        jw_write_envelope_error(&jw, op, target_id, now,
                                ERR_UNSUPPORTED_OPERATION, "Operation not supported by this collector", NULL, 0);
        ret = 1;
    }

    if (jw_has_overflow(&jw)) {
        fprintf(stderr, "[collector] Error: Output exceeded maximum allowed size (%d bytes)\n", MAX_OUTPUT_BUFFER);
        jw_init(&jw, out_buf, MAX_OUTPUT_BUFFER);
        jw_write_envelope_error(&jw, op, target_id, now,
                                ERR_RESPONSE_TOO_LARGE, "Response exceeded maximum output buffer size", NULL, 0);
        ret = 1;
    }

    /* Emit exactly one JSON document on stdout */
    fwrite(jw_get_buf(&jw), 1, jw_len(&jw), stdout);
    putchar('\n');
    fflush(stdout);

    free(out_buf);
    return ret;
}
