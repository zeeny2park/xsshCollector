/*
 * Glances Embedded Linux Collector
 *
 * SPDX-FileCopyrightText: 2026 Nicolas Hennion <nicolas@nicolargo.com>
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * System summary operation implementation.
 */

#include "sysinfo.h"
#include "protocol.h"
#include "json_writer.h"

#include <stdio.h>
#include <sys/utsname.h>
#include <unistd.h>

int collect_summary(json_writer_t *jw, const char *target_id) {
    double now = get_timestamp_now();
    jw_start_envelope_ok(jw, OP_SUMMARY, target_id, now);

    struct utsname uts;
    uname(&uts);

    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        snprintf(hostname, sizeof(hostname), "%s", target_id);
    }

    char distro[256];
    get_distro_name(distro, sizeof(distro));

    int cores = get_core_count();
    double uptime = get_uptime_seconds();

    jw_string(jw, "hostname", hostname);
    jw_string(jw, "os_name", uts.sysname);
    jw_string(jw, "os_version", uts.release);
    jw_string(jw, "platform", (sizeof(void *) == 8) ? "64bit" : "32bit");
    jw_string(jw, "linux_distro", distro);
    jw_int(jw, "uptime", (int64_t)uptime);

    /* Sample CPU */
    double cpu_tot = 0.0, cpu_usr = 0.0, cpu_sys = 0.0, cpu_idle = 100.0, cpu_io = 0.0;
    sample_cpu_usage(&cpu_tot, &cpu_usr, &cpu_sys, &cpu_idle, &cpu_io);

    jw_key_start_object(jw, "cpu");
    jw_double(jw, "total", cpu_tot, 1);
    jw_double(jw, "user", cpu_usr, 1);
    jw_double(jw, "system", cpu_sys, 1);
    jw_double(jw, "idle", cpu_idle, 1);
    jw_double(jw, "iowait", cpu_io, 1);
    jw_int(jw, "cpucore", cores);
    jw_end_object(jw); /* cpu */

    /* Memory */
    uint64_t m_tot = 0, m_avail = 0, m_used = 0, m_free = 0;
    double m_pct = 0.0;
    get_memory_info(&m_tot, &m_avail, &m_used, &m_free, &m_pct);

    jw_key_start_object(jw, "memory");
    jw_uint64(jw, "total", m_tot);
    jw_uint64(jw, "available", m_avail);
    jw_uint64(jw, "used", m_used);
    jw_uint64(jw, "free", m_free);
    jw_double(jw, "percent", m_pct, 1);
    jw_end_object(jw); /* memory */

    /* Load */
    double l1 = 0.0, l5 = 0.0, l15 = 0.0;
    get_load_averages(&l1, &l5, &l15);

    jw_key_start_object(jw, "load");
    jw_double(jw, "min1", l1, 2);
    jw_double(jw, "min5", l5, 2);
    jw_double(jw, "min15", l15, 2);
    jw_int(jw, "cpucore", cores);
    jw_end_object(jw); /* load */

    jw_end_envelope_ok(jw);
    return 0;
}
