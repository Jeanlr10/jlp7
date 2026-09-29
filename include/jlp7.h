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

/* ── Threading ──────────────────────────────────────────────────────── *
 *
 * The library is thread-safe under these rules:
 *
 *  1. Different threads may call jlp7_exec / jlp7_exec_ex / jlp7_run_*
 *     at the same time, provided each call uses its own Jlp7Env.
 *  2. A Jlp7Env (and a Jlp7Error) must not be used by two threads at the
 *     same time. The library does no locking on them. Hand one over
 *     between threads only with your own synchronisation.
 *  3. A Jlp7Config is only read during a call. Sharing one is fine as
 *     long as nobody changes it while a call is running.
 *  4. Python blocks run one at a time: they share one interpreter and
 *     take the GIL. C and Java blocks run in child processes and do run
 *     in parallel, and they do not hold the GIL while they run.
 *  5. The interpreter starts on the first Python block and is never
 *     finalised. If the process already runs Python (for example the
 *     library is loaded through ctypes), that interpreter is used.
 *  6. Python blocks run under PyGILState_Ensure, so the calling thread
 *     need not hold the GIL. Do not call into the library from a
 *     destructor or signal handler.
 */

/* ── Variable types ─────────────────────────────────────────────────── */

typedef enum {
    JLP7_INT,
    JLP7_FLOAT,
    JLP7_BOOL,
    JLP7_STRING,
    JLP7_ARRAY,   /* flat array of doubles; arr_len tracks count */
    JLP7_LIST,    /* ordered, mixed-type children; arr_len = child count */
    JLP7_DICT,    /* string-keyed children; arr_len = entry count */
    JLP7_NULL,    /* no value (Python None, JSON null) */
} Jlp7Type;

/* One value. An env variable, a dict entry and a list item are all a
 * Jlp7Var, so values nest to any depth.
 *
 *   name:  env variable -> the variable name
 *          dict entry   -> the key
 *          list item    -> NULL
 */
typedef struct Jlp7Var Jlp7Var;
struct Jlp7Var {
    char     *name;
    Jlp7Type  type;
    size_t    arr_len;  /* ARRAY: doubles; LIST/DICT: children */
    union {
        long long  i;
        double     f;
        int        b;   /* 0 = false, 1 = true */
        char      *s;
        double    *arr;
        Jlp7Var   *items;   /* LIST / DICT children */
    } val;
};

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
void      jlp7_env_dump(const Jlp7Env *env);   /* debug print */

/* Get the variable `name`, creating it if needed. Its old contents are
 * freed and it is left as JLP7_NULL. Fill it with the jlp7_var_set_*
 * functions below. */
Jlp7Var  *jlp7_env_slot(Jlp7Env *env, const char *name);

/* ── Building and reading nested values ─────────────────────────────── *
 *
 * Every jlp7_var_set_* frees what the value held before. Strings and
 * arrays are copied.
 *
 * Pointer lifetime: jlp7_env_slot, jlp7_env_set_* and jlp7_list_push /
 * jlp7_dict_put may move memory. A Jlp7Var* into an env is valid until
 * the next variable is added to that env. A child pointer is valid until
 * the next push/put on the same container. Fill a child completely
 * before you add its sibling.
 */

void      jlp7_var_set_int(Jlp7Var *v, long long val);
void      jlp7_var_set_float(Jlp7Var *v, double val);
void      jlp7_var_set_bool(Jlp7Var *v, int val);
void      jlp7_var_set_str(Jlp7Var *v, const char *val);
void      jlp7_var_set_array(Jlp7Var *v, const double *values, size_t len);
void      jlp7_var_set_null(Jlp7Var *v);
void      jlp7_var_set_list(Jlp7Var *v);   /* empty list */
void      jlp7_var_set_dict(Jlp7Var *v);   /* empty dict */

/* Append a new JLP7_NULL item to a list. NULL if v is not a list. */
Jlp7Var  *jlp7_list_push(Jlp7Var *list);
/* Add key to a dict (an existing key is reset). NULL if v is not a dict. */
Jlp7Var  *jlp7_dict_put(Jlp7Var *dict, const char *key);
Jlp7Var  *jlp7_list_at(const Jlp7Var *list, size_t index);
Jlp7Var  *jlp7_dict_get(const Jlp7Var *dict, const char *key);

/* Deep copy src into dst (dst keeps its own name). */
void      jlp7_var_copy(Jlp7Var *dst, const Jlp7Var *src);
/* Move src's value into dst (dst keeps its name; src's name is freed).
 * src is left as an unnamed JLP7_NULL. */
void      jlp7_var_move(Jlp7Var *dst, Jlp7Var *src);
/* Free what v holds and make it JLP7_NULL. Keeps the name. */
void      jlp7_var_clear(Jlp7Var *v);

/* Deep copy of an env. Returns NULL on allocation failure. */
Jlp7Env  *jlp7_env_clone(const Jlp7Env *env);
/* Copy every variable of src into dst (overwrites same-named vars). */
void      jlp7_env_merge(Jlp7Env *dst, const Jlp7Env *src);
/* Move the contents of src into dst and free src. dst's old contents
 * are freed. Used to roll an env back to a snapshot. */
void      jlp7_env_replace(Jlp7Env *dst, Jlp7Env *src);

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
