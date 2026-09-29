// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#ifndef JLP7_C_INTERNAL_H
#define JLP7_C_INTERNAL_H

#include <stdarg.h>
#include <stddef.h>
#include "jlp7.h"

#define JLP7_TYPE_MAX 32
#define JLP7_NAME_MAX 64

/* A single scanned variable declaration from a C block. */
typedef struct {
    char      type[JLP7_TYPE_MAX];
    char      name[JLP7_NAME_MAX];
    int       is_array;   /* 1 if declared as type name[N] */
    long long arr_len;    /* N, only meaningful when is_array or is_string */
    int       is_string;  /* char name[] / char name[N]: exported as a string */
} Jlp7CVarDecl;

/* ── Structs marked "// jlp7:export" ─────────────────────────────────── */

typedef enum {
    CF_INT,           /* integer types                    <-> JLP7_INT    */
    CF_FLOAT,         /* double, float                    <-> JLP7_FLOAT  */
    CF_BOOL,          /* bool, _Bool                      <-> JLP7_BOOL   */
    CF_STRING,        /* char name[N]                     <-> JLP7_STRING */
    CF_ARRAY,         /* number name[N]                   <-> JLP7_ARRAY  */
    CF_STRUCT,        /* another exported struct          <-> JLP7_DICT   */
    CF_STRUCT_ARRAY,  /* exported struct name[N]          <-> JLP7_LIST   */
} Jlp7CFieldKind;

typedef struct {
    char           name[JLP7_NAME_MAX];
    Jlp7CFieldKind kind;
    long long      len;    /* N for STRING / ARRAY / STRUCT_ARRAY */
    int            sidx;   /* index of the struct, for STRUCT kinds */
} Jlp7CField;

typedef struct {
    char        ctype[96];   /* how declarations spell it: "struct P" or "P" */
    Jlp7CField *fields;
    int         nfields;
} Jlp7CStruct;

/* A variable whose type is an exported struct (or an array of them). */
typedef struct {
    char      name[JLP7_NAME_MAX];
    int       sidx;
    long long arr_len;    /* 0 = single struct, N = array of N */
    int       has_init;   /* declared with "= ..." */
    size_t    stmt_end;   /* offset in body just after the ';' */
} Jlp7CStructVar;

/* Everything the builder needs to know about one C block. */
typedef struct {
    char           *body;      /* user code, exported definitions removed */
    char           *hoisted;   /* those definitions, for file scope */
    Jlp7CStruct    *structs;
    int             nstructs;
    Jlp7CVarDecl   *decls;
    int             ndecls;
    Jlp7CStructVar *svars;
    int             nsvars;
    char            error[200];   /* set when jlp7_c_scan returns -1 */
} Jlp7CScan;

/* c_builder.c */
Jlp7CVarDecl *jlp7_c_scan_decls(const char *code, int *count);
int           jlp7_c_scan(const char *code, Jlp7CScan *out);
void          jlp7_c_scan_free(Jlp7CScan *sc);
char         *jlp7_c_build_source(const Jlp7CScan *sc, const Jlp7Env *env);

/* c_json.c — reuses the Java JSON parser (same __VARS__ format) */
int  jlp7_c_parse_vars(const char *json, Jlp7Env *env,
                       char *why, size_t why_len);

#endif /* JLP7_C_INTERNAL_H */
