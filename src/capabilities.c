/*
 * Glances Embedded Linux Collector
 *
 * SPDX-FileCopyrightText: 2026 Nicolas Hennion <nicolas@nicolargo.com>
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Capabilities operation implementation.
 */

#include "sysinfo.h"
#include "protocol.h"
#include "json_writer.h"

#include <stdio.h>
#include <sys/utsname.h>
#include <unistd.h>

int collect_capabilities(json_writer_t *jw, const char *target_id) {
    double now = get_timestamp_now();
    jw_start_envelope_ok(jw, OP_CAPABILITIES, target_id, now);

    jw_string(jw, "collector_version", COLLECTOR_VERSION);

    jw_key_start_array(jw, "supported_operations");
    jw_arr_string(jw, OP_CAPABILITIES);
    jw_arr_string(jw, OP_SUMMARY);
    jw_arr_string(jw, OP_PROCESS_LIST);
    jw_arr_string(jw, OP_PROCESS_DETAILS);
    jw_end_array(jw);

    struct utsname uts;
    uname(&uts);

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        snprintf(hostname, sizeof(hostname), "%s", target_id);
    }

    jw_key_start_object(jw, "target_info");
    jw_string(jw, "os", uts.sysname);
    jw_string(jw, "kernel", uts.release);
    jw_string(jw, "arch", uts.machine);
    jw_string(jw, "hostname", hostname);
    jw_end_object(jw); /* target_info */

    jw_end_envelope_ok(jw);
    return 0;
}
