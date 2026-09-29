// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jlp7.h"

#define INITIAL_CAP 16

/* ── Values ─────────────────────────────────────────────────────────── */

/* List/dict child arrays are allocated with room for items_cap(len)
 * entries, so a push only reallocates when len reaches a power of two.
 * This keeps big lists linear without a separate capacity field. */
static size_t items_cap(size_t len) {
    size_t c = 4;
    while (c < len) c *= 2;
    return c;
}

static void free_children(Jlp7Var *v) {
    for (size_t i = 0; i < v->arr_len; i++) {
        free(v->val.items[i].name);
        jlp7_var_clear(&v->val.items[i]);
    }
    free(v->val.items);
}

void jlp7_var_clear(Jlp7Var *v) {
    switch (v->type) {
        case JLP7_STRING: free(v->val.s);   break;
        case JLP7_ARRAY:  free(v->val.arr); break;
        case JLP7_LIST:
        case JLP7_DICT:   free_children(v); break;
        default: break;
    }
    v->type    = JLP7_NULL;
    v->arr_len = 0;
    v->val.i   = 0;
}

void jlp7_var_set_int(Jlp7Var *v, long long val) {
    jlp7_var_clear(v);
    v->type  = JLP7_INT;
    v->val.i = val;
}

void jlp7_var_set_float(Jlp7Var *v, double val) {
    jlp7_var_clear(v);
    v->type  = JLP7_FLOAT;
    v->val.f = val;
}

void jlp7_var_set_bool(Jlp7Var *v, int val) {
    jlp7_var_clear(v);
    v->type  = JLP7_BOOL;
    v->val.b = val ? 1 : 0;
}

void jlp7_var_set_str(Jlp7Var *v, const char *val) {
    char *copy = strdup(val ? val : "");   /* val may point into v itself */
    jlp7_var_clear(v);
    v->type  = JLP7_STRING;
    v->val.s = copy;
}

void jlp7_var_set_array(Jlp7Var *v, const double *values, size_t len) {
    double *copy = malloc(sizeof(double) * (len ? len : 1));
    if (len && copy) memcpy(copy, values, sizeof(double) * len);
    jlp7_var_clear(v);
    v->type    = JLP7_ARRAY;
    v->arr_len = len;
    v->val.arr = copy;
}

void jlp7_var_set_null(Jlp7Var *v) { jlp7_var_clear(v); }

void jlp7_var_set_list(Jlp7Var *v) {
    jlp7_var_clear(v);
    v->type      = JLP7_LIST;
    v->val.items = NULL;
}

void jlp7_var_set_dict(Jlp7Var *v) {
    jlp7_var_clear(v);
    v->type      = JLP7_DICT;
    v->val.items = NULL;
}

static Jlp7Var *append_child(Jlp7Var *c, const char *key) {
    if (c->arr_len == 0 || c->arr_len == items_cap(c->arr_len)) {
        size_t ncap = c->arr_len == 0 ? 4 : c->arr_len * 2;
        Jlp7Var *ni = realloc(c->val.items, sizeof(Jlp7Var) * ncap);
        if (!ni) return NULL;
        c->val.items = ni;
    }
    Jlp7Var *child = &c->val.items[c->arr_len];
    memset(child, 0, sizeof(*child));
    child->type = JLP7_NULL;
    child->name = key ? strdup(key) : NULL;
    c->arr_len++;
    return child;
}

Jlp7Var *jlp7_list_push(Jlp7Var *list) {
    if (!list || list->type != JLP7_LIST) return NULL;
    return append_child(list, NULL);
}

Jlp7Var *jlp7_dict_get(const Jlp7Var *dict, const char *key) {
    if (!dict || dict->type != JLP7_DICT) return NULL;
    for (size_t i = 0; i < dict->arr_len; i++)
        if (strcmp(dict->val.items[i].name, key) == 0)
            return &dict->val.items[i];
    return NULL;
}

Jlp7Var *jlp7_dict_put(Jlp7Var *dict, const char *key) {
    if (!dict || dict->type != JLP7_DICT) return NULL;
    Jlp7Var *existing = jlp7_dict_get(dict, key);
    if (existing) {
        jlp7_var_clear(existing);
        return existing;
    }
    return append_child(dict, key);
}

Jlp7Var *jlp7_list_at(const Jlp7Var *list, size_t index) {
    if (!list || list->type != JLP7_LIST || index >= list->arr_len) return NULL;
    return &list->val.items[index];
}

void jlp7_var_copy(Jlp7Var *dst, const Jlp7Var *src) {
    /* Build the copy first: dst may be inside src's tree. */
    Jlp7Var tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.type = JLP7_NULL;

    switch (src->type) {
        case JLP7_INT:    tmp.type = JLP7_INT;   tmp.val.i = src->val.i; break;
        case JLP7_FLOAT:  tmp.type = JLP7_FLOAT; tmp.val.f = src->val.f; break;
        case JLP7_BOOL:   tmp.type = JLP7_BOOL;  tmp.val.b = src->val.b; break;
        case JLP7_NULL:   break;
        case JLP7_STRING:
            tmp.type  = JLP7_STRING;
            tmp.val.s = strdup(src->val.s ? src->val.s : "");
            break;
        case JLP7_ARRAY:
            tmp.type    = JLP7_ARRAY;
            tmp.arr_len = src->arr_len;
            tmp.val.arr = malloc(sizeof(double) * (src->arr_len ? src->arr_len : 1));
            if (src->arr_len)
                memcpy(tmp.val.arr, src->val.arr, sizeof(double) * src->arr_len);
            break;
        case JLP7_LIST:
        case JLP7_DICT:
            tmp.type = src->type;
            if (src->arr_len) {
                tmp.val.items = malloc(sizeof(Jlp7Var) * items_cap(src->arr_len));
                for (size_t i = 0; i < src->arr_len; i++) {
                    Jlp7Var *c = &tmp.val.items[i];
                    memset(c, 0, sizeof(*c));
                    c->type = JLP7_NULL;
                    c->name = src->val.items[i].name
                                ? strdup(src->val.items[i].name) : NULL;
                    jlp7_var_copy(c, &src->val.items[i]);
                }
                tmp.arr_len = src->arr_len;
            }
            break;
    }
    jlp7_var_move(dst, &tmp);
}

void jlp7_var_move(Jlp7Var *dst, Jlp7Var *src) {
    char *keep = dst->name;
    jlp7_var_clear(dst);
    free(src->name);          /* src's own name is dropped */
    *dst      = *src;
    dst->name = keep;
    src->name = NULL;
    src->type = JLP7_NULL;
    src->arr_len = 0;
    src->val.i = 0;
}

/* ── Env ────────────────────────────────────────────────────────────── */

Jlp7Env *jlp7_env_new(void) {
    Jlp7Env *env = malloc(sizeof(Jlp7Env));
    if (!env) return NULL;
    env->vars  = malloc(sizeof(Jlp7Var) * INITIAL_CAP);
    env->count = 0;
    env->cap   = INITIAL_CAP;
    return env;
}

void jlp7_env_free(Jlp7Env *env) {
    if (!env) return;
    for (size_t i = 0; i < env->count; i++) {
        free(env->vars[i].name);
        jlp7_var_clear(&env->vars[i]);
    }
    free(env->vars);
    free(env);
}

/* Find existing slot by name, or append a new one. */
static Jlp7Var *upsert(Jlp7Env *env, const char *name) {
    for (size_t i = 0; i < env->count; i++)
        if (strcmp(env->vars[i].name, name) == 0)
            return &env->vars[i];

    if (env->count == env->cap) {
        size_t ncap = env->cap * 2;
        Jlp7Var *nv = realloc(env->vars, sizeof(Jlp7Var) * ncap);
        if (!nv) return NULL;
        env->vars = nv;
        env->cap  = ncap;
    }
    Jlp7Var *v = &env->vars[env->count++];
    memset(v, 0, sizeof(*v));
    v->name = strdup(name);
    v->type = JLP7_NULL;
    return v;
}

Jlp7Var *jlp7_env_slot(Jlp7Env *env, const char *name) {
    Jlp7Var *v = upsert(env, name);
    if (v) jlp7_var_clear(v);
    return v;
}

void jlp7_env_set_int(Jlp7Env *env, const char *name, long long val) {
    Jlp7Var *v = upsert(env, name);
    if (v) jlp7_var_set_int(v, val);
}

void jlp7_env_set_float(Jlp7Env *env, const char *name, double val) {
    Jlp7Var *v = upsert(env, name);
    if (v) jlp7_var_set_float(v, val);
}

void jlp7_env_set_bool(Jlp7Env *env, const char *name, int val) {
    Jlp7Var *v = upsert(env, name);
    if (v) jlp7_var_set_bool(v, val);
}

void jlp7_env_set_str(Jlp7Env *env, const char *name, const char *val) {
    Jlp7Var *v = upsert(env, name);
    if (v) jlp7_var_set_str(v, val);
}

void jlp7_env_set_array(Jlp7Env *env, const char *name,
                         const double *values, size_t len) {
    Jlp7Var *v = upsert(env, name);
    if (v) jlp7_var_set_array(v, values, len);
}

Jlp7Var *jlp7_env_get(Jlp7Env *env, const char *name) {
    for (size_t i = 0; i < env->count; i++)
        if (strcmp(env->vars[i].name, name) == 0)
            return &env->vars[i];
    return NULL;
}

void jlp7_env_merge(Jlp7Env *dst, const Jlp7Env *src) {
    for (size_t i = 0; i < src->count; i++) {
        Jlp7Var *slot = upsert(dst, src->vars[i].name);
        if (slot) jlp7_var_copy(slot, &src->vars[i]);
    }
}

Jlp7Env *jlp7_env_clone(const Jlp7Env *env) {
    Jlp7Env *c = jlp7_env_new();
    if (c) jlp7_env_merge(c, env);
    return c;
}

void jlp7_env_replace(Jlp7Env *dst, Jlp7Env *src) {
    for (size_t i = 0; i < dst->count; i++) {
        free(dst->vars[i].name);
        jlp7_var_clear(&dst->vars[i]);
    }
    free(dst->vars);
    *dst = *src;   /* take over the buffer */
    free(src);
}

/* ── Debug ──────────────────────────────────────────────────────────── */

static void dump_value(const Jlp7Var *v, int depth) {
    switch (v->type) {
        case JLP7_INT:    fprintf(stderr, "%lld", v->val.i); break;
        case JLP7_FLOAT:  fprintf(stderr, "%g", v->val.f); break;
        case JLP7_BOOL:   fputs(v->val.b ? "true" : "false", stderr); break;
        case JLP7_STRING: fprintf(stderr, "\"%s\"", v->val.s); break;
        case JLP7_NULL:   fputs("null", stderr); break;
        case JLP7_ARRAY:
            fprintf(stderr, "[%zu doubles]", v->arr_len);
            break;
        case JLP7_LIST:
        case JLP7_DICT: {
            int is_dict = v->type == JLP7_DICT;
            fputc(is_dict ? '{' : '[', stderr);
            if (depth >= 3 && v->arr_len) {
                fputs("...", stderr);
            } else {
                for (size_t i = 0; i < v->arr_len; i++) {
                    if (i) fputs(", ", stderr);
                    if (is_dict) fprintf(stderr, "%s: ", v->val.items[i].name);
                    dump_value(&v->val.items[i], depth + 1);
                }
            }
            fputc(is_dict ? '}' : ']', stderr);
            break;
        }
    }
}

void jlp7_env_dump(const Jlp7Env *env) {
    fprintf(stderr, "[jlp7:env] %zu variable(s):\n", env->count);
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];
        fprintf(stderr, "  %s = ", v->name);
        dump_value(v, 0);
        fputc('\n', stderr);
    }
}
