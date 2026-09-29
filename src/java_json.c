// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include "jlp7.h"
#include "java_internal.h"

/*
 * java_json.c — JSON parser for the __VARS__ line that the C and Java
 * runners print. (The C runner shares it; see c_json.c.)
 *
 * Input:  {"key": value, ...}
 * Values: string, integer, float, true, false, null, [array], {object}
 *
 * Mapping:
 *   integer            -> JLP7_INT      (float if it does not fit long long)
 *   number with . e E  -> JLP7_FLOAT
 *   NaN, Infinity      -> JLP7_FLOAT    (also nan, inf, -inf, as printf writes)
 *   array of numbers   -> JLP7_ARRAY    (also the empty array)
 *   any other array    -> JLP7_LIST
 *   object             -> JLP7_DICT
 *   null               -> JLP7_NULL
 */

#define MAX_DEPTH 64

typedef struct {
    const char *p;
    char        why[96];   /* set on failure */
} Parser;

static int fail(Parser *ps, const char *why) {
    if (!ps->why[0]) snprintf(ps->why, sizeof(ps->why), "%s", why);
    return -1;
}

static void skip_ws(Parser *ps) {
    while (*ps->p && isspace((unsigned char)*ps->p)) ps->p++;
}

static int hex4(const char *s, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        if (!isxdigit((unsigned char)s[i])) return -1;
        char c = s[i];
        v = v * 16 + (unsigned)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
    }
    *out = v;
    return 0;
}

static char *put_utf8(char *dst, unsigned cp) {
    if (cp < 0x80) {
        *dst++ = (char)cp;
    } else if (cp < 0x800) {
        *dst++ = (char)(0xC0 | (cp >> 6));
        *dst++ = (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *dst++ = (char)(0xE0 | (cp >> 12));
        *dst++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *dst++ = (char)(0x80 | (cp & 0x3F));
    } else {
        *dst++ = (char)(0xF0 | (cp >> 18));
        *dst++ = (char)(0x80 | ((cp >> 12) & 0x3F));
        *dst++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *dst++ = (char)(0x80 | (cp & 0x3F));
    }
    return dst;
}

/* Parse a JSON string. The result is never longer than the input, so one
 * allocation of the raw length is enough. Caller frees. NULL on error. */
static char *parse_string(Parser *ps) {
    const char *p = ps->p;
    if (*p != '"') { fail(ps, "expected string"); return NULL; }
    p++;

    const char *end = p;
    while (*end && *end != '"') {
        if (*end == '\\' && end[1]) end++;
        end++;
    }
    if (*end != '"') { fail(ps, "unterminated string"); return NULL; }

    char *out = malloc((size_t)(end - p) + 1);
    char *dst = out;
    while (p < end) {
        if (*p != '\\') { *dst++ = *p++; continue; }
        p++;
        switch (*p) {
            case '"':  *dst++ = '"';  p++; break;
            case '\\': *dst++ = '\\'; p++; break;
            case '/':  *dst++ = '/';  p++; break;
            case 'b':  *dst++ = '\b'; p++; break;
            case 'f':  *dst++ = '\f'; p++; break;
            case 'n':  *dst++ = '\n'; p++; break;
            case 'r':  *dst++ = '\r'; p++; break;
            case 't':  *dst++ = '\t'; p++; break;
            case 'u': {
                unsigned cp, lo;
                if (p + 5 > end || hex4(p + 1, &cp) != 0) {
                    free(out); fail(ps, "bad \\u escape"); return NULL;
                }
                p += 5;
                if (cp >= 0xD800 && cp < 0xDC00 && p + 6 <= end &&
                    p[0] == '\\' && p[1] == 'u' && hex4(p + 2, &lo) == 0 &&
                    lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                }
                dst = put_utf8(dst, cp);
                break;
            }
            default: *dst++ = *p++; break;
        }
    }
    *dst = '\0';
    ps->p = end + 1;
    return out;
}

static int parse_value(Parser *ps, Jlp7Var *out, int depth);

/* An array is a JLP7_ARRAY when every element is a plain number. */
static void arrayify_if_numeric(Jlp7Var *list) {
    for (size_t i = 0; i < list->arr_len; i++) {
        Jlp7Type t = list->val.items[i].type;
        if (t != JLP7_INT && t != JLP7_FLOAT) return;
    }
    double *nums = malloc(sizeof(double) * (list->arr_len ? list->arr_len : 1));
    for (size_t i = 0; i < list->arr_len; i++) {
        const Jlp7Var *c = &list->val.items[i];
        nums[i] = c->type == JLP7_INT ? (double)c->val.i : c->val.f;
    }
    jlp7_var_set_array(list, nums, list->arr_len);
    free(nums);
}

static int parse_number(Parser *ps, Jlp7Var *out) {
    const char *p = ps->p;
    char *end;

    /* Non-finite spellings from Java (NaN, Infinity) and printf (nan, inf). */
    const char *q = p + (*p == '-' || *p == '+');
    if (strncasecmp(q, "nan", 3) == 0 && !isalnum((unsigned char)q[3])) {
        jlp7_var_set_float(out, strtod("nan", NULL));
        ps->p = q + 3;
        return 0;
    }
    if (strncasecmp(q, "infinity", 8) == 0 || strncasecmp(q, "inf", 3) == 0) {
        size_t n = strncasecmp(q, "infinity", 8) == 0 ? 8 : 3;
        jlp7_var_set_float(out, *p == '-' ? -strtod("inf", NULL) : strtod("inf", NULL));
        ps->p = q + n;
        return 0;
    }

    /* Decide integer vs float from the token, not from strtoll's stop point. */
    const char *t = p;
    if (*t == '-') t++;
    if (!isdigit((unsigned char)*t)) return fail(ps, "bad number");
    while (isdigit((unsigned char)*t)) t++;
    int is_float = (*t == '.' || *t == 'e' || *t == 'E');

    if (!is_float) {
        errno = 0;
        long long iv = strtoll(p, &end, 10);
        if (errno != ERANGE) {
            jlp7_var_set_int(out, iv);
            ps->p = end;
            return 0;
        }
    }
    double fv = strtod(p, &end);
    if (end == p) return fail(ps, "bad number");
    jlp7_var_set_float(out, fv);
    ps->p = end;
    return 0;
}

static int parse_value(Parser *ps, Jlp7Var *out, int depth) {
    if (depth > MAX_DEPTH) return fail(ps, "nesting too deep");
    skip_ws(ps);
    const char *p = ps->p;

    if (*p == '"') {
        char *s = parse_string(ps);
        if (!s) return -1;
        jlp7_var_set_str(out, s);
        free(s);
        return 0;
    }

    if (*p == '[') {
        ps->p++;
        jlp7_var_set_list(out);
        skip_ws(ps);
        if (*ps->p == ']') {
            ps->p++;
            arrayify_if_numeric(out);   /* [] -> empty array */
            return 0;
        }
        for (;;) {
            Jlp7Var *item = jlp7_list_push(out);
            if (!item) return fail(ps, "out of memory");
            if (parse_value(ps, item, depth + 1) != 0) return -1;
            skip_ws(ps);
            if (*ps->p == ',') { ps->p++; continue; }
            if (*ps->p == ']') { ps->p++; break; }
            return fail(ps, "expected , or ] in array");
        }
        arrayify_if_numeric(out);
        return 0;
    }

    if (*p == '{') {
        ps->p++;
        jlp7_var_set_dict(out);
        skip_ws(ps);
        if (*ps->p == '}') { ps->p++; return 0; }
        for (;;) {
            skip_ws(ps);
            char *key = parse_string(ps);
            if (!key) return -1;
            skip_ws(ps);
            if (*ps->p != ':') { free(key); return fail(ps, "expected : after key"); }
            ps->p++;
            Jlp7Var *slot = jlp7_dict_put(out, key);
            free(key);
            if (!slot) return fail(ps, "out of memory");
            if (parse_value(ps, slot, depth + 1) != 0) return -1;
            skip_ws(ps);
            if (*ps->p == ',') { ps->p++; continue; }
            if (*ps->p == '}') { ps->p++; break; }
            return fail(ps, "expected , or } in object");
        }
        return 0;
    }

    if (strncmp(p, "true", 4) == 0 && !isalnum((unsigned char)p[4])) {
        jlp7_var_set_bool(out, 1); ps->p += 4; return 0;
    }
    if (strncmp(p, "false", 5) == 0 && !isalnum((unsigned char)p[5])) {
        jlp7_var_set_bool(out, 0); ps->p += 5; return 0;
    }
    if (strncmp(p, "null", 4) == 0 && !isalnum((unsigned char)p[4])) {
        jlp7_var_set_null(out); ps->p += 4; return 0;
    }
    if (*p == '-' || *p == '+' || isalnum((unsigned char)*p))
        return parse_number(ps, out);

    return fail(ps, "unexpected character");
}

/* Parse the top-level object into env. Variables not named in the JSON
 * are left alone. Returns 0 on success, -1 if the JSON is malformed
 * (with a short reason in why, if why != NULL). */
int jlp7_java_parse_vars(const char *json, Jlp7Env *env,
                         char *why, size_t why_len) {
    Parser ps = { json, "" };
    skip_ws(&ps);
    if (*ps.p != '{') {
        fail(&ps, "expected {");
    } else {
        Jlp7Var top;
        memset(&top, 0, sizeof(top));
        top.type = JLP7_NULL;
        int ok = parse_value(&ps, &top, 0) == 0 && top.type == JLP7_DICT;
        if (ok) {
            /* Commit only now that the whole line parsed. */
            for (size_t i = 0; i < top.arr_len; i++) {
                char *nm = strdup(top.val.items[i].name);
                Jlp7Var *slot = jlp7_env_slot(env, nm);
                if (slot) jlp7_var_move(slot, &top.val.items[i]);
                free(nm);
            }
        }
        jlp7_var_clear(&top);
        if (ok) return 0;
    }
    if (why && why_len) snprintf(why, why_len, "%s", ps.why[0] ? ps.why : "not a JSON object");
    return -1;
}
