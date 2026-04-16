// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jlp7.h"

#define INITIAL_CAP 16

Jlp7Env *jlp7_env_new(void) {
    Jlp7Env *env = malloc(sizeof(Jlp7Env));
    if (!env) return NULL;
    env->vars  = malloc(sizeof(Jlp7Var) * INITIAL_CAP);
    env->count = 0;
    env->cap   = INITIAL_CAP;
    return env;
}

static void var_free(Jlp7Var *v) {
    free(v->name);
    if (v->type == JLP7_STRING) free(v->val.s);
}

void jlp7_env_free(Jlp7Env *env) {
    if (!env) return;
    for (size_t i = 0; i < env->count; i++) var_free(&env->vars[i]);
    free(env->vars);
    free(env);
}

/* Find existing slot by name, or append a new one. */
static Jlp7Var *upsert(Jlp7Env *env, const char *name) {
    for (size_t i = 0; i < env->count; i++)
        if (strcmp(env->vars[i].name, name) == 0)
            return &env->vars[i];

    if (env->count == env->cap) {
        env->cap *= 2;
        env->vars = realloc(env->vars, sizeof(Jlp7Var) * env->cap);
        if (!env->vars) return NULL;
    }
    Jlp7Var *v = &env->vars[env->count++];
    v->name    = strdup(name);
    v->type    = JLP7_INT;   /* default; overwritten below */
    v->val.i   = 0;
    return v;
}

/* Free the string payload of a var that's being overwritten. */
static void clear_str(Jlp7Var *v) {
    if (v->type == JLP7_STRING) {
        free(v->val.s);
        v->val.s = NULL;
    }
}

void jlp7_env_set_int(Jlp7Env *env, const char *name, long long val) {
    Jlp7Var *v = upsert(env, name);
    if (!v) return;
    clear_str(v);
    v->type  = JLP7_INT;
    v->val.i = val;
}

void jlp7_env_set_float(Jlp7Env *env, const char *name, double val) {
    Jlp7Var *v = upsert(env, name);
    if (!v) return;
    clear_str(v);
    v->type  = JLP7_FLOAT;
    v->val.f = val;
}

void jlp7_env_set_bool(Jlp7Env *env, const char *name, int val) {
    Jlp7Var *v = upsert(env, name);
    if (!v) return;
    clear_str(v);
    v->type  = JLP7_BOOL;
    v->val.b = val ? 1 : 0;
}

void jlp7_env_set_str(Jlp7Env *env, const char *name, const char *val) {
    Jlp7Var *v = upsert(env, name);
    if (!v) return;
    clear_str(v);
    v->type  = JLP7_STRING;
    v->val.s = strdup(val);
}

Jlp7Var *jlp7_env_get(Jlp7Env *env, const char *name) {
    for (size_t i = 0; i < env->count; i++)
        if (strcmp(env->vars[i].name, name) == 0)
            return &env->vars[i];
    return NULL;
}

void jlp7_env_dump(const Jlp7Env *env) {
    fprintf(stderr, "[jlp7:env] %zu variable(s):\n", env->count);
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];
        switch (v->type) {
            case JLP7_INT:    fprintf(stderr, "  %s = %lld (int)\n",   v->name, v->val.i); break;
            case JLP7_FLOAT:  fprintf(stderr, "  %s = %g (float)\n",   v->name, v->val.f); break;
            case JLP7_BOOL:   fprintf(stderr, "  %s = %s (bool)\n",    v->name, v->val.b ? "true" : "false"); break;
            case JLP7_STRING: fprintf(stderr, "  %s = \"%s\" (str)\n", v->name, v->val.s); break;
        }
    }
}
