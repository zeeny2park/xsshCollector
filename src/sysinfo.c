/*
 * Glances Embedded Linux Collector
 *
 * SPDX-FileCopyrightText: 2026 Nicolas Hennion <nicolas@nicolargo.com>
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * System information collection utilities implementation.
 */

#include "sysinfo.h"
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <inttypes.h>

double get_timestamp_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (double)ts.tv_sec + ((double)ts.tv_nsec / 1e9);
    }
    return (double)time(NULL);
}

double get_uptime_seconds(void) {
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) return 0.0;
    double uptime = 0.0;
    if (fscanf(fp, "%lf", &uptime) != 1) {
        uptime = 0.0;
    }
    fclose(fp);
    return uptime;
}

int get_core_count(void) {
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    return (cores > 0) ? (int)cores : 1;
}

void get_distro_name(char *dst, size_t size) {
    if (!dst || size == 0) return;
    snprintf(dst, size, "Linux");

    FILE *fp = fopen("/etc/os-release", "r");
    if (!fp) return;

    char line[MAX_LINE_BUFFER];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
            char *val = line + 12;
            if (*val == '"' || *val == '\'') val++;
            size_t len = strlen(val);
            while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r' || val[len - 1] == '"' || val[len - 1] == '\'')) {
                val[--len] = '\0';
            }
            if (len > 0) {
                snprintf(dst, size, "%s", val);
                break;
            }
        }
    }
    fclose(fp);
}

int get_memory_info(uint64_t *total, uint64_t *available, uint64_t *used, uint64_t *free_mem, double *percent) {
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1;

    uint64_t m_total = 0, m_free = 0, m_avail = 0, m_buffers = 0, m_cached = 0;
    bool has_avail = false;
    char line[MAX_LINE_BUFFER];

    while (fgets(line, sizeof(line), fp)) {
        uint64_t val = 0;
        if (sscanf(line, "MemTotal: %" SCNu64 " kB", &val) == 1) {
            m_total = val * 1024;
        } else if (sscanf(line, "MemFree: %" SCNu64 " kB", &val) == 1) {
            m_free = val * 1024;
        } else if (sscanf(line, "MemAvailable: %" SCNu64 " kB", &val) == 1) {
            m_avail = val * 1024;
            has_avail = true;
        } else if (sscanf(line, "Buffers: %" SCNu64 " kB", &val) == 1) {
            m_buffers = val * 1024;
        } else if (sscanf(line, "Cached: %" SCNu64 " kB", &val) == 1) {
            m_cached = val * 1024;
        }
    }
    fclose(fp);

    if (!has_avail) {
        m_avail = m_free + m_buffers + m_cached;
    }

    uint64_t m_used = (m_total > m_avail) ? (m_total - m_avail) : 0;
    double m_pct = (m_total > 0) ? (((double)m_used / (double)m_total) * 100.0) : 0.0;

    if (total) *total = m_total;
    if (available) *available = m_avail;
    if (used) *used = m_used;
    if (free_mem) *free_mem = m_free;
    if (percent) *percent = m_pct;

    return 0;
}

int get_load_averages(double *min1, double *min5, double *min15) {
    FILE *fp = fopen("/proc/loadavg", "r");
    if (!fp) return -1;
    double l1 = 0.0, l5 = 0.0, l15 = 0.0;
    if (fscanf(fp, "%lf %lf %lf", &l1, &l5, &l15) != 3) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    if (min1) *min1 = l1;
    if (min5) *min5 = l5;
    if (min15) *min15 = l15;
    return 0;
}

typedef struct {
    uint64_t user;
    uint64_t nice;
    uint64_t system;
    uint64_t idle;
    uint64_t iowait;
    uint64_t irq;
    uint64_t softirq;
    uint64_t steal;
} cpu_raw_ticks_t;

static int read_cpu_ticks(cpu_raw_ticks_t *t) {
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1;
    char line[MAX_LINE_BUFFER];
    int parsed = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "cpu ", 4) == 0) {
            parsed = sscanf(line + 4, "%" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                            &t->user, &t->nice, &t->system, &t->idle,
                            &t->iowait, &t->irq, &t->softirq, &t->steal);
            break;
        }
    }
    fclose(fp);
    return (parsed >= 4) ? 0 : -1;
}

int sample_cpu_usage(double *total_pct, double *user_pct, double *system_pct, double *idle_pct, double *iowait_pct) {
    cpu_raw_ticks_t t1, t2;
    memset(&t1, 0, sizeof(t1));
    memset(&t2, 0, sizeof(t2));

    if (read_cpu_ticks(&t1) != 0) {
        return -1;
    }

    /* Brief 50ms pause to calculate active delta */
    struct timespec req = {0, 50 * 1000 * 1000}; /* 50 ms */
    nanosleep(&req, NULL);

    if (read_cpu_ticks(&t2) != 0) {
        return -1;
    }

    uint64_t user_diff = (t2.user + t2.nice) - (t1.user + t1.nice);
    uint64_t system_diff = (t2.system + t2.irq + t2.softirq) - (t1.system + t1.irq + t1.softirq);
    uint64_t idle_diff = t2.idle - t1.idle;
    uint64_t iowait_diff = t2.iowait - t1.iowait;
    uint64_t steal_diff = t2.steal - t1.steal;

    uint64_t total_diff = user_diff + system_diff + idle_diff + iowait_diff + steal_diff;
    if (total_diff == 0) total_diff = 1;

    double u_pct = ((double)user_diff / (double)total_diff) * 100.0;
    double s_pct = ((double)system_diff / (double)total_diff) * 100.0;
    double i_pct = ((double)idle_diff / (double)total_diff) * 100.0;
    double io_pct = ((double)iowait_diff / (double)total_diff) * 100.0;
    double tot_pct = u_pct + s_pct + io_pct;

    if (tot_pct > 100.0) tot_pct = 100.0;
    if (tot_pct < 0.0) tot_pct = 0.0;

    if (total_pct) *total_pct = tot_pct;
    if (user_pct) *user_pct = u_pct;
    if (system_pct) *system_pct = s_pct;
    if (idle_pct) *idle_pct = i_pct;
    if (iowait_pct) *iowait_pct = io_pct;

    return 0;
}

void resolve_username(uid_t uid, char *dst, size_t size) {
    if (!dst || size == 0) return;
    struct passwd *pw = getpwuid(uid);
    if (pw && pw->pw_name) {
        snprintf(dst, size, "%s", pw->pw_name);
    } else {
        snprintf(dst, size, "%u", (unsigned int)uid);
    }
}
