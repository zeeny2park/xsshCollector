/*
 * Glances Embedded Linux Collector
 *
 * SPDX-FileCopyrightText: 2026 Nicolas Hennion <nicolas@nicolargo.com>
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Safe, bounded JSON stream formatter implementation.
 */

#include "json_writer.h"
#include "protocol.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

void jw_init(json_writer_t *jw, char *buffer, size_t capacity) {
    jw->buf = buffer;
    jw->capacity = capacity;
    jw->len = 0;
    jw->depth = 0;
    jw->overflow = false;
    if (capacity > 0) {
        jw->buf[0] = '\0';
    } else {
        jw->overflow = true;
    }
    for (int i = 0; i < JW_MAX_DEPTH; i++) {
        jw->need_comma[i] = false;
    }
}

size_t jw_len(const json_writer_t *jw) {
    return jw->len;
}

const char *jw_get_buf(const json_writer_t *jw) {
    return jw->buf;
}

bool jw_has_overflow(const json_writer_t *jw) {
    return jw->overflow;
}

static void jw_append_char(json_writer_t *jw, char c) {
    if (jw->overflow) return;
    if (jw->len + 1 >= jw->capacity) {
        jw->overflow = true;
        return;
    }
    jw->buf[jw->len++] = c;
    jw->buf[jw->len] = '\0';
}

static void jw_append_raw(json_writer_t *jw, const char *str) {
    if (jw->overflow || !str) return;
    size_t slen = strlen(str);
    if (jw->len + slen >= jw->capacity) {
        jw->overflow = true;
        return;
    }
    memcpy(jw->buf + jw->len, str, slen);
    jw->len += slen;
    jw->buf[jw->len] = '\0';
}

static void jw_write_escaped_string(json_writer_t *jw, const char *str) {
    jw_append_char(jw, '"');
    if (!str) {
        jw_append_char(jw, '"');
        return;
    }

    const unsigned char *p = (const unsigned char *)str;
    while (*p && !jw->overflow) {
        switch (*p) {
            case '"':  jw_append_raw(jw, "\\\""); break;
            case '\\': jw_append_raw(jw, "\\\\"); break;
            case '\b': jw_append_raw(jw, "\\b");  break;
            case '\f': jw_append_raw(jw, "\\f");  break;
            case '\n': jw_append_raw(jw, "\\n");  break;
            case '\r': jw_append_raw(jw, "\\r");  break;
            case '\t': jw_append_raw(jw, "\\t");  break;
            default:
                if (*p < 0x20) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\u%04x", *p);
                    jw_append_raw(jw, hex);
                } else {
                    jw_append_char(jw, (char)*p);
                }
                break;
        }
        p++;
    }
    jw_append_char(jw, '"');
}

static void jw_prefix_comma(json_writer_t *jw) {
    if (jw->depth > 0 && jw->need_comma[jw->depth]) {
        jw_append_char(jw, ',');
    }
}

void jw_start_object(json_writer_t *jw) {
    jw_prefix_comma(jw);
    jw_append_char(jw, '{');
    if (jw->depth < JW_MAX_DEPTH - 1) {
        jw->depth++;
        jw->need_comma[jw->depth] = false;
    } else {
        jw->overflow = true;
    }
}

void jw_end_object(json_writer_t *jw) {
    jw_append_char(jw, '}');
    if (jw->depth > 0) {
        jw->depth--;
        jw->need_comma[jw->depth] = true;
    }
}

void jw_start_array(json_writer_t *jw) {
    jw_prefix_comma(jw);
    jw_append_char(jw, '[');
    if (jw->depth < JW_MAX_DEPTH - 1) {
        jw->depth++;
        jw->need_comma[jw->depth] = false;
    } else {
        jw->overflow = true;
    }
}

void jw_end_array(json_writer_t *jw) {
    jw_append_char(jw, ']');
    if (jw->depth > 0) {
        jw->depth--;
        jw->need_comma[jw->depth] = true;
    }
}

void jw_key(json_writer_t *jw, const char *key) {
    jw_prefix_comma(jw);
    jw_write_escaped_string(jw, key);
    jw_append_char(jw, ':');
    jw->need_comma[jw->depth] = true;
}

void jw_string(json_writer_t *jw, const char *key, const char *val) {
    jw_key(jw, key);
    jw_write_escaped_string(jw, val);
}

void jw_int(json_writer_t *jw, const char *key, int64_t val) {
    jw_key(jw, key);
    char buf[32];
    snprintf(buf, sizeof(buf), "%" PRId64, val);
    jw_append_raw(jw, buf);
}

void jw_uint64(json_writer_t *jw, const char *key, uint64_t val) {
    jw_key(jw, key);
    char buf[32];
    snprintf(buf, sizeof(buf), "%" PRIu64, val);
    jw_append_raw(jw, buf);
}

void jw_double(json_writer_t *jw, const char *key, double val, int precision) {
    jw_key(jw, key);
    char fmt[16];
    char buf[64];
    snprintf(fmt, sizeof(fmt), "%%.%df", precision);
    snprintf(buf, sizeof(buf), fmt, val);
    jw_append_raw(jw, buf);
}

void jw_bool(json_writer_t *jw, const char *key, bool val) {
    jw_key(jw, key);
    jw_append_raw(jw, val ? "true" : "false");
}

void jw_null(json_writer_t *jw, const char *key) {
    jw_key(jw, key);
    jw_append_raw(jw, "null");
}

void jw_key_start_object(json_writer_t *jw, const char *key) {
    jw_key(jw, key);
    jw_append_char(jw, '{');
    if (jw->depth < JW_MAX_DEPTH - 1) {
        jw->depth++;
        jw->need_comma[jw->depth] = false;
    } else {
        jw->overflow = true;
    }
}

void jw_key_start_array(json_writer_t *jw, const char *key) {
    jw_key(jw, key);
    jw_append_char(jw, '[');
    if (jw->depth < JW_MAX_DEPTH - 1) {
        jw->depth++;
        jw->need_comma[jw->depth] = false;
    } else {
        jw->overflow = true;
    }
}

void jw_arr_string(json_writer_t *jw, const char *val) {
    jw_prefix_comma(jw);
    jw_write_escaped_string(jw, val);
    jw->need_comma[jw->depth] = true;
}

void jw_arr_int(json_writer_t *jw, int64_t val) {
    jw_prefix_comma(jw);
    char buf[32];
    snprintf(buf, sizeof(buf), "%" PRId64, val);
    jw_append_raw(jw, buf);
    jw->need_comma[jw->depth] = true;
}

void jw_arr_double(json_writer_t *jw, double val, int precision) {
    jw_prefix_comma(jw);
    char fmt[16];
    char buf[64];
    snprintf(fmt, sizeof(fmt), "%%.%df", precision);
    snprintf(buf, sizeof(buf), fmt, val);
    jw_append_raw(jw, buf);
    jw->need_comma[jw->depth] = true;
}

void jw_arr_start_object(json_writer_t *jw) {
    jw_prefix_comma(jw);
    jw_append_char(jw, '{');
    if (jw->depth < JW_MAX_DEPTH - 1) {
        jw->depth++;
        jw->need_comma[jw->depth] = false;
    } else {
        jw->overflow = true;
    }
}

void jw_arr_start_array(json_writer_t *jw) {
    jw_prefix_comma(jw);
    jw_append_char(jw, '[');
    if (jw->depth < JW_MAX_DEPTH - 1) {
        jw->depth++;
        jw->need_comma[jw->depth] = false;
    } else {
        jw->overflow = true;
    }
}

void jw_start_envelope_ok(json_writer_t *jw, const char *op, const char *target_id, double timestamp) {
    jw_start_object(jw);
    jw_string(jw, "protocol_version", PROTOCOL_VERSION);
    jw_string(jw, "status", "ok");
    jw_string(jw, "operation", op);
    jw_string(jw, "target_id", target_id);
    jw_double(jw, "timestamp", timestamp, 3);
    jw_string(jw, "collector_version", COLLECTOR_VERSION);

    jw_key_start_array(jw, "capabilities");
    jw_arr_string(jw, OP_CAPABILITIES);
    jw_arr_string(jw, OP_SUMMARY);
    jw_arr_string(jw, OP_PROCESS_LIST);
    jw_arr_string(jw, OP_PROCESS_DETAILS);
    jw_end_array(jw);

    jw_key_start_object(jw, "data");
}

void jw_end_envelope_ok(json_writer_t *jw) {
    jw_end_object(jw); /* end of data */
    jw_end_object(jw); /* end of root envelope */
}

void jw_write_envelope_error(json_writer_t *jw, const char *op, const char *target_id, double timestamp,
                             const char *err_code, const char *err_msg, const char *opt_details_key, int64_t opt_details_val) {
    jw_start_object(jw);
    jw_string(jw, "protocol_version", PROTOCOL_VERSION);
    jw_string(jw, "status", "error");
    jw_string(jw, "operation", op);
    jw_string(jw, "target_id", target_id);
    jw_double(jw, "timestamp", timestamp, 3);
    jw_string(jw, "collector_version", COLLECTOR_VERSION);

    jw_key_start_array(jw, "capabilities");
    jw_arr_string(jw, OP_CAPABILITIES);
    jw_arr_string(jw, OP_SUMMARY);
    jw_arr_string(jw, OP_PROCESS_LIST);
    jw_arr_string(jw, OP_PROCESS_DETAILS);
    jw_end_array(jw);

    jw_key_start_object(jw, "error");
    jw_string(jw, "code", err_code);
    jw_string(jw, "message", err_msg);
    if (opt_details_key != NULL) {
        jw_key_start_object(jw, "details");
        jw_int(jw, opt_details_key, opt_details_val);
        jw_end_object(jw);
    } else {
        jw_null(jw, "details");
    }
    jw_end_object(jw); /* error */

    jw_end_object(jw); /* root envelope */
}
