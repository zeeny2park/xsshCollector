/*
 * Glances Embedded Linux Collector
 *
 * SPDX-FileCopyrightText: 2026 Nicolas Hennion <nicolas@nicolargo.com>
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Protocol v1.0 constants, structures, and module declarations.
 */

#ifndef GLANCES_COLLECTOR_PROTOCOL_H
#define GLANCES_COLLECTOR_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define PROTOCOL_VERSION "1.0"
#define COLLECTOR_VERSION "1.0.0"

/* Bounded limits to prevent denial of service or unbounded allocation */
#define MAX_OUTPUT_BUFFER (2 * 1024 * 1024)  /* 2 MB */
#define MAX_LINE_BUFFER 2048
#define MAX_PATH_BUFFER 512
#define MAX_CMDLINE_LEN 2048
#define MAX_PROCS_LIST 1000

/* Standard protocol operations */
#define OP_CAPABILITIES "capabilities"
#define OP_SUMMARY "summary"
#define OP_PROCESS_LIST "process_list"
#define OP_PROCESS_DETAILS "process_details"

/* Standard error codes matching Glances Protocol v1.0 */
#define ERR_INVALID_REQUEST "INVALID_REQUEST"
#define ERR_UNSUPPORTED_OPERATION "UNSUPPORTED_OPERATION"
#define ERR_PROCESS_NOT_FOUND "PROCESS_NOT_FOUND"
#define ERR_PERMISSION_DENIED "PERMISSION_DENIED"
#define ERR_INTERNAL_ERROR "INTERNAL_ERROR"
#define ERR_RESPONSE_TOO_LARGE "RESPONSE_TOO_LARGE"

#endif /* GLANCES_COLLECTOR_PROTOCOL_H */
