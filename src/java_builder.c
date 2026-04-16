// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "jlp7.h"
#include "dynstr.h"
#include "java_internal.h"

/*
 * java_builder.c — builds the JShell source fed to the Java runner.
 *
 * Two responsibilities:
 *   1. jlp7_java_scan_decls   — scan a block for variable declarations
 *   2. jlp7_java_build_source — assemble preamble + user code + JSON printer
 */

/* ── Variable declaration scanner ───────────────────────────────────── */

static const char *JAVA_PRIMITIVES[] = {
    "int","long","double","float","boolean","String","char","short","byte", NULL
};

static int is_str_type(const char *t) {
    return strcmp(t, "String") == 0 || strcmp(t, "char") == 0;
}

/* Returns a heap-allocated array of VarDecl; *count is set to the length.
 * Caller must free the array. */
Jlp7VarDecl *jlp7_java_scan_decls(const char *code, int *count) {
    size_t       cap   = 32;
    Jlp7VarDecl *decls = malloc(sizeof(Jlp7VarDecl) * cap);
    *count = 0;

    const char *line = code;
    while (*line) {
        while (*line == ' ' || *line == '\t') line++;

        for (int t = 0; JAVA_PRIMITIVES[t]; t++) {
            size_t tlen = strlen(JAVA_PRIMITIVES[t]);
            if (strncmp(line, JAVA_PRIMITIVES[t], tlen) == 0 &&
                (line[tlen] == ' ' || line[tlen] == '\t')) {

                const char *after = line + tlen;
                while (*after == ' ' || *after == '\t') after++;

                char name[64] = {0};
                size_t ni = 0;
                while (*after && (isalnum((unsigned char)*after) || *after == '_') && ni < 63)
                    name[ni++] = *after++;

                if (ni > 0 && (*after == ' ' || *after == '=' || *after == ';')) {
                    if ((size_t)*count == cap) {
                        cap *= 2;
                        decls = realloc(decls, sizeof(Jlp7VarDecl) * cap);
                    }
                    strncpy(decls[*count].type, JAVA_PRIMITIVES[t], JLP7_TYPE_MAX - 1);
                    strncpy(decls[*count].name, name, JLP7_NAME_MAX - 1);
                    (*count)++;
                }
                break;
            }
        }
        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
    return decls;
}

/* ── Source assembler ───────────────────────────────────────────────── */

/*
 * Assemble the full JShell source:
 *   [preamble: env vars as Java declarations]
 *   [user code]
 *   [JSON printer: System.out.println("__VARS__:{...}")]
 *
 * Caller must free the returned string.
 */
char *jlp7_java_build_source(const char *code,
                              const Jlp7Env *env,
                              const Jlp7VarDecl *decls,
                              int ndecls) {
    DynStr *src = ds_new();

    /* ── Preamble: inject env vars that aren't redeclared ── */
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];

        /* Skip if this block redeclares it */
        int redeclared = 0;
        for (int j = 0; j < ndecls; j++)
            if (strcmp(decls[j].name, v->name) == 0) { redeclared = 1; break; }
        if (redeclared) continue;

        switch (v->type) {
            case JLP7_INT:
                ds_appendf(src, "long %s = %lldL;\n", v->name, v->val.i);
                break;
            case JLP7_FLOAT:
                ds_appendf(src, "double %s = %g;\n", v->name, v->val.f);
                break;
            case JLP7_BOOL:
                ds_appendf(src, "boolean %s = %s;\n",
                           v->name, v->val.b ? "true" : "false");
                break;
            case JLP7_STRING:
                ds_appendf(src, "String %s = \"", v->name);
                ds_append_c_escaped(src, v->val.s);
                ds_append(src, "\";\n");
                break;
        }
    }

    /* ── User code ── */
    ds_append(src, code);
    ds_append(src, "\n");

    /* ── JSON printer ── */
    /* Collect all vars to export: new decls + inherited env vars */
    ds_append(src, "System.out.println(\"__VARS__:{\" +\n");

    int first = 1;

    /* New decls from this block */
    for (int i = 0; i < ndecls; i++) {
        if (!first) ds_append(src, " + \", \" +\n");
        if (is_str_type(decls[i].type)) {
            ds_appendf(src, "  \"\\\"" "%s" "\\\": \\\"\" + %s + \"\\\"\"",
                       decls[i].name, decls[i].name);
        } else {
            ds_appendf(src, "  \"\\\"" "%s" "\\\": \" + %s",
                       decls[i].name, decls[i].name);
        }
        first = 0;
    }

    /* Inherited env vars (not redeclared) */
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];
        int redeclared = 0;
        for (int j = 0; j < ndecls; j++)
            if (strcmp(decls[j].name, v->name) == 0) { redeclared = 1; break; }
        if (redeclared) continue;

        if (!first) ds_append(src, " + \", \" +\n");
        int strtype = (v->type == JLP7_STRING);
        if (strtype)
            ds_appendf(src, "  \"\\\"" "%s" "\\\": \\\"\" + %s + \"\\\"\"",
                       v->name, v->name);
        else
            ds_appendf(src, "  \"\\\"" "%s" "\\\": \" + %s",
                       v->name, v->name);
        first = 0;
    }

    if (first)
        ds_append(src, "  \"\"");   /* no variables at all */

    ds_append(src, " + \"}\");\n");

    return ds_take(src);
}
