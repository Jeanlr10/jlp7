// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#ifndef JLP7_C_INTERNAL_H
#define JLP7_C_INTERNAL_H

#include <stdarg.h>
#include "jlp7.h"

#define JLP7_TYPE_MAX 32
#define JLP7_NAME_MAX 64

/* A single scanned variable declaration from a C block. */
typedef struct {
    char type[JLP7_TYPE_MAX];
    char name[JLP7_NAME_MAX];
} Jlp7CVarDecl;

/* c_builder.c */
Jlp7CVarDecl *jlp7_c_scan_decls(const char *code, int *count);
char         *jlp7_c_build_source(const char *code,
                                   const Jlp7Env *env,
                                   const Jlp7CVarDecl *decls,
                                   int ndecls);

/* c_json.c — reuses the Java JSON parser (same __VARS__ format) */
void jlp7_c_parse_vars(const char *json, Jlp7Env *env);

#endif /* JLP7_C_INTERNAL_H */
