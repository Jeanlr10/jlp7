// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#ifndef JLP7_H
#define JLP7_H

#include <stddef.h>

/* ╔══════════════════════════════════════════════════════════════════╗
 * ║  JLP7 — Jean-Luc's Practical Purposeful Pre-Processed           ║
 * ║         Polyglot Python Project                                  ║
 * ╚══════════════════════════════════════════════════════════════════╝
 *
 * Public API. Include this header and link against libjlp7.so.
 */

/* ── Variable types ─────────────────────────────────────────────────── */

typedef enum {
    JLP7_INT,
    JLP7_FLOAT,
    JLP7_BOOL,
    JLP7_STRING,
} Jlp7Type;

typedef struct {
    char     *name;
    Jlp7Type  type;
    union {
        long long  i;
        double     f;
        int        b;   /* 0 = false, 1 = true */
        char      *s;
    } val;
} Jlp7Var;

/* ── Variable store ─────────────────────────────────────────────────── */

typedef struct {
    Jlp7Var *vars;
    size_t   count;
    size_t   cap;
} Jlp7Env;

Jlp7Env  *jlp7_env_new(void);
void      jlp7_env_free(Jlp7Env *env);
void      jlp7_env_set_int(Jlp7Env *env, const char *name, long long val);
void      jlp7_env_set_float(Jlp7Env *env, const char *name, double val);
void      jlp7_env_set_bool(Jlp7Env *env, const char *name, int val);
void      jlp7_env_set_str(Jlp7Env *env, const char *name, const char *val);
Jlp7Var  *jlp7_env_get(Jlp7Env *env, const char *name);
void      jlp7_env_dump(const Jlp7Env *env);   /* debug print */

/* ── Block types ────────────────────────────────────────────────────── */

typedef enum {
    JLP7_BLOCK_FOREIGN,
    JLP7_BLOCK_PYTHON,
} Jlp7BlockType;

typedef struct Jlp7Block {
    Jlp7BlockType    type;
    char            *code;
    struct Jlp7Block *next;
} Jlp7Block;

Jlp7Block *jlp7_parse(const char *source);
void       jlp7_blocks_free(Jlp7Block *head);

/* ── Language runners ───────────────────────────────────────────────── */

int jlp7_run_python(const char *code, Jlp7Env *env);
int jlp7_run_java(const char *code, Jlp7Env *env);
int jlp7_run_c(const char *code, Jlp7Env *env);

/* ── Top-level config & executor ────────────────────────────────────── */

typedef struct {
    const char *language;   /* e.g. "java" */
    int         allowpy;    /* 0 = reject /p...p/ blocks */
    int         debug;      /* 1 = verbose internal logging */
} Jlp7Config;

/* Execute a JLP7 polyglot source string.
 * Returns 0 on success, -1 on error.
 * Variable state accumulates in env across all blocks. */
int jlp7_exec(const char *source, Jlp7Config *cfg, Jlp7Env *env);

/* Convenience: default config (java, allowpy=1, debug=0) */
Jlp7Config jlp7_default_config(const char *language);

#endif /* JLP7_H */
