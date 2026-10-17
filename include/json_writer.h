/*
 * Glances Embedded Linux Collector
 *
 * SPDX-FileCopyrightText: 2026 Nicolas Hennion <nicolas@nicolargo.com>
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Safe, bounded JSON stream formatter.
 */

#ifndef GLANCES_COLLECTOR_JSON_WRITER_H
#define GLANCES_COLLECTOR_JSON_WRITER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define JW_MAX_DEPTH 64

typedef struct {
    char *buf;
    size_t capacity;
    size_t len;
    int depth;
    bool need_comma[JW_MAX_DEPTH];
    bool overflow;
} json_writer_t;

void jw_init(json_writer_t *jw, char *buffer, size_t capacity);
size_t jw_len(const json_writer_t *jw);
const char *jw_get_buf(const json_writer_t *jw);
bool jw_has_overflow(const json_writer_t *jw);

/* Structural elements */
void jw_start_object(json_writer_t *jw);
void jw_end_object(json_writer_t *jw);
void jw_start_array(json_writer_t *jw);
void jw_end_array(json_writer_t *jw);

/* Object key-value pairs */
void jw_key(json_writer_t *jw, const char *key);
void jw_string(json_writer_t *jw, const char *key, const char *val);
void jw_int(json_writer_t *jw, const char *key, int64_t val);
void jw_uint64(json_writer_t *jw, const char *key, uint64_t val);
void jw_double(json_writer_t *jw, const char *key, double val, int precision);
void jw_bool(json_writer_t *jw, const char *key, bool val);
void jw_null(json_writer_t *jw, const char *key);

/* Nested objects/arrays with keys */
void jw_key_start_object(json_writer_t *jw, const char *key);
void jw_key_start_array(json_writer_t *jw, const char *key);

/* Array elements (no key) */
void jw_arr_string(json_writer_t *jw, const char *val);
void jw_arr_int(json_writer_t *jw, int64_t val);
void jw_arr_double(json_writer_t *jw, double val, int precision);
void jw_arr_start_object(json_writer_t *jw);
void jw_arr_start_array(json_writer_t *jw);

/* Standard Glances envelopes */
void jw_start_envelope_ok(json_writer_t *jw, const char *op, const char *target_id, double timestamp);
void jw_end_envelope_ok(json_writer_t *jw);
void jw_write_envelope_error(json_writer_t *jw, const char *op, const char *target_id, double timestamp,
                             const char *err_code, const char *err_msg, const char *opt_details_key, int64_t opt_details_val);

#endif /* GLANCES_COLLECTOR_JSON_WRITER_H */
