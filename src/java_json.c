// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "jlp7.h"
#include "java_internal.h"

/*
 * java_json.c — minimal JSON parser for JShell __VARS__ output.
 *
 * Handles: strings (with escape sequences), integers, floats, booleans.
 * Input format: {"key": value, "key2": value2, ...}
 */

static void skip_ws(const char **p) {
    while (**p && isspace((unsigned char)**p)) (*p)++;
}

/* Parse a JSON string, handling escape sequences.
 * Caller must free the returned string. */
static char *parse_json_string(const char **p) {
    if (**p != '"') return NULL;
    (*p)++;

    /* First pass: calculate length needed */
    const char *scan = *p;
    size_t len = 0;
    while (*scan && *scan != '"') {
        if (*scan == '\\') {
            scan++;   /* skip escape char */
            if (!*scan) break;
        }
        scan++;
        len++;
    }

    char *out = malloc(len + 1);
    char *dst = out;
    while (**p && **p != '"') {
        if (**p == '\\') {
            (*p)++;
            switch (**p) {
                case '"':  *dst++ = '"';  break;
                case '\\': *dst++ = '\\'; break;
                case '/':  *dst++ = '/';  break;
                case 'n':  *dst++ = '\n'; break;
                case 'r':  *dst++ = '\r'; break;
                case 't':  *dst++ = '\t'; break;
                default:   *dst++ = **p; break;
            }
        } else {
            *dst++ = **p;
        }
        (*p)++;
    }
    *dst = '\0';
    if (**p == '"') (*p)++;
    return out;
}

void jlp7_java_parse_vars(const char *json, Jlp7Env *env) {
    const char *p = json;
    skip_ws(&p);
    if (*p != '{') return;
    p++;

    while (*p && *p != '}') {
        skip_ws(&p);
        if (*p != '"') { p++; continue; }

        char *key = parse_json_string(&p);
        if (!key) break;

        skip_ws(&p);
        if (*p == ':') p++;
        skip_ws(&p);

        if (*p == '"') {
            char *val = parse_json_string(&p);
            jlp7_env_set_str(env, key, val);
            free(val);
        } else if (strncmp(p, "true", 4) == 0 && !isalnum((unsigned char)p[4])) {
            jlp7_env_set_bool(env, key, 1);
            p += 4;
        } else if (strncmp(p, "false", 5) == 0 && !isalnum((unsigned char)p[5])) {
            jlp7_env_set_bool(env, key, 0);
            p += 5;
        } else if (*p == '-' || isdigit((unsigned char)*p)) {
            char *end;
            long long ival = strtoll(p, &end, 10);
            if (*end == '.' || *end == 'e' || *end == 'E') {
                double fval = strtod(p, &end);
                jlp7_env_set_float(env, key, fval);
            } else {
                jlp7_env_set_int(env, key, ival);
            }
            p = end;
        }

        free(key);
        skip_ws(&p);
        if (*p == ',') p++;
    }
}
