// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#ifndef JLP7_JAVA_INTERNAL_H
#define JLP7_JAVA_INTERNAL_H

#include <stdarg.h>
#include "jlp7.h"

#define JLP7_TYPE_MAX 32
#define JLP7_NAME_MAX 64

/* A single scanned variable declaration from a Java block. */
typedef struct {
    char type[JLP7_TYPE_MAX];
    char name[JLP7_NAME_MAX];
} Jlp7VarDecl;

/* java_json.c */
void jlp7_java_parse_vars(const char *json, Jlp7Env *env);

/* java_builder.c */
Jlp7VarDecl *jlp7_java_scan_decls(const char *code, int *count);
char        *jlp7_java_build_source(const char *code,
                                     const Jlp7Env *env,
                                     const Jlp7VarDecl *decls,
                                     int ndecls);

#endif /* JLP7_JAVA_INTERNAL_H */
