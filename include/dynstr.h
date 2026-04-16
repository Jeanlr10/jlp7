// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#ifndef JLP7_DYNSTR_H
#define JLP7_DYNSTR_H

/*
 * dynstr.h — lightweight dynamic string builder.
 * Used by language-specific source assemblers to avoid fixed-buffer truncation.
 * All functions are static inline so this header is self-contained.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} DynStr;

static inline DynStr *ds_new(void) {
    DynStr *d = malloc(sizeof(DynStr));
    d->cap    = 4096;
    d->buf    = malloc(d->cap);
    d->buf[0] = '\0';
    d->len    = 0;
    return d;
}

static inline void ds_append(DynStr *d, const char *s) {
    size_t add = strlen(s);
    while (d->len + add + 1 > d->cap) {
        d->cap *= 2;
        d->buf  = realloc(d->buf, d->cap);
    }
    memcpy(d->buf + d->len, s, add + 1);
    d->len += add;
}

static inline void ds_appendf(DynStr *d, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    while (d->len + (size_t)need + 1 > d->cap) {
        d->cap *= 2;
        d->buf  = realloc(d->buf, d->cap);
    }
    vsnprintf(d->buf + d->len, d->cap - d->len, fmt, ap);
    va_end(ap);
    d->len += need;
}

/* Transfer ownership of the buffer to the caller. Frees the DynStr shell. */
static inline char *ds_take(DynStr *d) {
    char *s = d->buf;
    free(d);
    return s;
}

/* Append a C-escaped string literal body (no surrounding quotes). */
static inline void ds_append_c_escaped(DynStr *d, const char *s) {
    for (; *s; s++) {
        switch (*s) {
            case '\\': ds_append(d, "\\\\"); break;
            case '"':  ds_append(d, "\\\""); break;
            case '\n': ds_append(d, "\\n");  break;
            case '\r': ds_append(d, "\\r");  break;
            case '\t': ds_append(d, "\\t");  break;
            default:   ds_appendf(d, "%c", *s); break;
        }
    }
}

#endif /* JLP7_DYNSTR_H */
