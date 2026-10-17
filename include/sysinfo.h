/*
 * Glances Embedded Linux Collector
 *
 * SPDX-FileCopyrightText: 2026 Nicolas Hennion <nicolas@nicolargo.com>
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * System information collection utilities header.
 */

#ifndef GLANCES_COLLECTOR_SYSINFO_H
#define GLANCES_COLLECTOR_SYSINFO_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "json_writer.h"

double get_timestamp_now(void);
double get_uptime_seconds(void);
int get_core_count(void);
void get_distro_name(char *dst, size_t size);
int get_memory_info(uint64_t *total, uint64_t *available, uint64_t *used, uint64_t *free, double *percent);
int get_load_averages(double *min1, double *min5, double *min15);
int sample_cpu_usage(double *total_pct, double *user_pct, double *system_pct, double *idle_pct, double *iowait_pct);
void resolve_username(uid_t uid, char *dst, size_t size);

/* Collector operation handlers */
int collect_capabilities(json_writer_t *jw, const char *target_id);
int collect_summary(json_writer_t *jw, const char *target_id);
int collect_process_list(json_writer_t *jw, const char *target_id);
int collect_process_details(json_writer_t *jw, const char *target_id, int pid);

#endif /* GLANCES_COLLECTOR_SYSINFO_H */
