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
    JLP7_ARRAY,   /* array of doubles, flattened; arr_len tracks count */
} Jlp7Type;

typedef struct {
    char     *name;
    Jlp7Type  type;
    size_t    arr_len;  /* only meaningful when type == JLP7_ARRAY */
    union {
        long long  i;
        double     f;
        int        b;   /* 0 = false, 1 = true */
        char      *s;
        double    *arr;
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
void      jlp7_env_set_array(Jlp7Env *env, const char *name,
                              const double *values, size_t len);
Jlp7Var  *jlp7_env_get(Jlp7Env *env, const char *name);

/* Deep copy of an env. Returns NULL on allocation failure. */
Jlp7Env  *jlp7_env_clone(const Jlp7Env *env);
/* Copy every variable of src into dst (overwrites same-named vars). */
void      jlp7_env_merge(Jlp7Env *dst, const Jlp7Env *src);
/* Move the contents of src into dst and free src. dst's old contents
 * are freed. Used to roll an env back to a snapshot. */
void      jlp7_env_replace(Jlp7Env *dst, Jlp7Env *src);
void      jlp7_env_dump(const Jlp7Env *env);   /* debug print */

/* ── Block types ────────────────────────────────────────────────────── */

typedef enum {
    JLP7_BLOCK_FOREIGN,
    JLP7_BLOCK_PYTHON,
} Jlp7BlockType;

typedef struct Jlp7Block {
    Jlp7BlockType    type;
    char            *code;
    int              line;   /* 1-based line in the original source where
                              * this block's code starts */
    struct Jlp7Block *next;
} Jlp7Block;

Jlp7Block *jlp7_parse(const char *source);
void       jlp7_blocks_free(Jlp7Block *head);

/* ── Errors ─────────────────────────────────────────────────────────── */

typedef enum {
    JLP7_ERR_NONE = 0,
    JLP7_ERR_CONFIG,           /* bad config: unsupported language, allowpy=0 */
    JLP7_ERR_PY_COMPILE,       /* SyntaxError in a /p...p/ block */
    JLP7_ERR_PY_RUNTIME,       /* exception raised while running a block */
    JLP7_ERR_MARSHAL,          /* value cannot cross the language boundary */
    JLP7_ERR_FOREIGN,          /* the C or Java block failed */
    JLP7_ERR_INTERNAL,         /* out of memory, temp file failure, etc. */
} Jlp7ErrorKind;

/* All string fields are heap-allocated and owned by the struct.
 * Zero-initialise before use, and call jlp7_error_clear() when done. */
typedef struct {
    Jlp7ErrorKind kind;
    char *exc_type;     /* Python exception class name, or NULL */
    char *message;      /* short message (str(exception)), or NULL */
    char *traceback;    /* full formatted traceback, or NULL */
    int   block_index;  /* 0-based index of the failing block, -1 if n/a */
    int   line;         /* 1-based line in the original source, 0 if unknown */
} Jlp7Error;

void        jlp7_error_clear(Jlp7Error *err);
const char *jlp7_error_kind_name(Jlp7ErrorKind kind);

/* ── Language runners ───────────────────────────────────────────────── */

/* Run one Python block. On failure returns -1, leaves env untouched, and
 * (when err != NULL) fills err. With err == NULL the message goes to
 * stderr instead. line_offset is the 1-based source line where the block
 * starts, so tracebacks show lines of the original file (use 1 if
 * unknown). strict != 0 turns a variable that cannot be represented in
 * the env (dict, object, non-numeric list) into a JLP7_ERR_MARSHAL. */
int jlp7_run_python_ex(const char *code, Jlp7Env *env, int line_offset,
                       int strict, Jlp7Error *err);
int jlp7_run_python(const char *code, Jlp7Env *env);
int jlp7_run_java(const char *code, Jlp7Env *env);
int jlp7_run_c(const char *code, Jlp7Env *env);

/* ── Top-level config & executor ────────────────────────────────────── */

typedef struct {
    const char *language;   /* e.g. "java" */
    int         allowpy;    /* 0 = reject /p...p/ blocks */
    int         debug;      /* 1 = verbose internal logging */
    int         strict;     /* 1 = unrepresentable Python values are errors */
} Jlp7Config;

/* Execute a JLP7 polyglot source string.
 * Returns 0 on success, -1 on error.
 * Variable state accumulates in env across all blocks.
 *
 * Transactional: if any block fails, env is restored to exactly the
 * state it had before the call. */
int jlp7_exec(const char *source, Jlp7Config *cfg, Jlp7Env *env);

/* Same as jlp7_exec, and on failure fills *err (may be NULL) instead of
 * printing to stderr. Foreign-block output on stderr is unchanged. */
int jlp7_exec_ex(const char *source, Jlp7Config *cfg, Jlp7Env *env,
                 Jlp7Error *err);

/* Convenience: default config (java, allowpy=1, debug=0, strict=0) */
Jlp7Config jlp7_default_config(const char *language);

#endif /* JLP7_H */
