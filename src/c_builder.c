// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "jlp7.h"
#include "dynstr.h"
#include "c_internal.h"

/*
 * c_builder.c — generates a self-contained C program from a JLP7 block.
 *
 * The generated program:
 *   1. #includes for stdio, stdlib, string
 *   2. Preamble: env vars as global declarations
 *   3. User code inside main()
 *   4. printf(__VARS__:{...}) at the end of main()
 *
 * C type mapping:
 *   JLP7_INT    → long long   (%lld)
 *   JLP7_FLOAT  → double      (%g)
 *   JLP7_BOOL   → int         (0/1, printed as true/false)
 *   JLP7_STRING → char[]      (%s, quoted in JSON output)
 */

/* ── C type primitives we recognise in user code ────────────────────── */

static const char *C_TYPES[] = {
    "int", "long", "double", "float", "char", "short",
    "unsigned", "signed", "bool", "_Bool",
    /* common typedefs */
    "int8_t", "int16_t", "int32_t", "int64_t",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "size_t", "ssize_t", "ptrdiff_t",
    NULL
};

/* Map a C type string to the closest Jlp7Type for marshalling back. */
static Jlp7Type c_type_to_jlp7(const char *t) {
    if (strcmp(t, "double") == 0 || strcmp(t, "float") == 0)
        return JLP7_FLOAT;
    if (strcmp(t, "bool") == 0 || strcmp(t, "_Bool") == 0)
        return JLP7_BOOL;
    /* char* / char[] handled as string by the runner; plain char as int */
    return JLP7_INT;
}

/* ── Variable declaration scanner ───────────────────────────────────── */

/*
 * Scans for simple declarations of the form:
 *   <type> <name> [= ...] ;
 *
 * Does NOT handle:
 *   - pointer types (char *p)  — too ambiguous without a real parser
 *   - multi-declarators (int a, b)
 *   - struct/union/enum
 *
 * These are "best effort" — unknown vars are simply not exported.
 */
Jlp7CVarDecl *jlp7_c_scan_decls(const char *code, int *count) {
    size_t        cap   = 32;
    Jlp7CVarDecl *decls = malloc(sizeof(Jlp7CVarDecl) * cap);
    *count = 0;

    const char *line = code;
    while (*line) {
        /* skip leading whitespace */
        while (*line == ' ' || *line == '\t') line++;

        /* skip preprocessor directives and comments */
        if (*line == '#' || (line[0] == '/' && line[1] == '/') ||
            (line[0] == '/' && line[1] == '*')) {
            while (*line && *line != '\n') line++;
            if (*line) line++;
            continue;
        }

        for (int t = 0; C_TYPES[t]; t++) {
            size_t tlen = strlen(C_TYPES[t]);
            if (strncmp(line, C_TYPES[t], tlen) == 0 &&
                (line[tlen] == ' ' || line[tlen] == '\t')) {

                const char *after = line + tlen;

                /* skip "long long", "unsigned int", etc. */
                while (*after == ' ' || *after == '\t') after++;
                for (int t2 = 0; C_TYPES[t2]; t2++) {
                    size_t t2len = strlen(C_TYPES[t2]);
                    if (strncmp(after, C_TYPES[t2], t2len) == 0 &&
                        (after[t2len] == ' ' || after[t2len] == '\t')) {
                        after += t2len;
                        break;
                    }
                }
                while (*after == ' ' || *after == '\t') after++;

                /* skip pointer stars — we don't export pointer vars */
                if (*after == '*') {
                    while (*line && *line != '\n') line++;
                    if (*line) line++;
                    break;
                }

                /* read name */
                char name[JLP7_NAME_MAX] = {0};
                size_t ni = 0;
                while (*after && (isalnum((unsigned char)*after) || *after == '_')
                       && ni < JLP7_NAME_MAX - 1)
                    name[ni++] = *after++;

                if (ni > 0 && (*after == ' ' || *after == '=' || *after == ';')) {
                    if ((size_t)*count == cap) {
                        cap *= 2;
                        decls = realloc(decls, sizeof(Jlp7CVarDecl) * cap);
                    }
                    strncpy(decls[*count].type, C_TYPES[t], JLP7_TYPE_MAX - 1);
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
 * Build the complete C program:
 *
 *   #include <stdio.h>
 *   #include <stdlib.h>
 *   #include <string.h>
 *
 *   // --- preamble: env vars ---
 *   long long x = 6;
 *   double pi = 3.14;
 *   char name[] = "jlp7";
 *
 *   int main(void) {
 *       // --- user code ---
 *       int result = (int)x * 2;
 *
 *       // --- JSON printer ---
 *       printf("__VARS__:{");
 *       printf("\"x\": %lld", x);
 *       printf(", \"result\": %d", result);
 *       printf("}\n");
 *       return 0;
 *   }
 */
char *jlp7_c_build_source(const char *code,
                           const Jlp7Env *env,
                           const Jlp7CVarDecl *decls,
                           int ndecls) {
    DynStr *src = ds_new();

    /* ── Headers ── */
    ds_append(src,
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdbool.h>\n"
        "\n");

    /* ── Preamble globals: env vars not redeclared in this block ── */
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];
        int redeclared = 0;
        for (int j = 0; j < ndecls; j++)
            if (strcmp(decls[j].name, v->name) == 0) { redeclared = 1; break; }
        if (redeclared) continue;

        switch (v->type) {
            case JLP7_INT:
                ds_appendf(src, "long long %s = %lldLL;\n", v->name, v->val.i);
                break;
            case JLP7_FLOAT:
                ds_appendf(src, "double %s = %.17g;\n", v->name, v->val.f);
                break;
            case JLP7_BOOL:
                ds_appendf(src, "int %s = %d;\n", v->name, v->val.b);
                break;
            case JLP7_STRING:
                ds_appendf(src, "char %s[] = \"", v->name);
                ds_append_c_escaped(src, v->val.s);
                ds_append(src, "\";\n");
                break;
        }
    }
    ds_append(src, "\n");

    /* ── main() + user code ── */
    ds_append(src, "int main(void) {\n");
    ds_append(src, code);
    ds_append(src, "\n");

    /* ── JSON printer ── */
    ds_append(src, "    printf(\"__VARS__:{\");\n");

    int first = 1;

    /* Helper macro — we just emit printf calls */
#define EMIT_VAR(namestr, fmt, varexpr) \
    do { \
        if (!first) ds_append(src, "    printf(\", \");\n"); \
        ds_appendf(src, "    printf(\"\\\"" namestr "\\\": " fmt "\", " varexpr ");\n"); \
        first = 0; \
    } while(0)

    /* Newly declared vars in this block */
    for (int i = 0; i < ndecls; i++) {
        const char *n = decls[i].name;
        Jlp7Type jtype = c_type_to_jlp7(decls[i].type);
        if (!first) ds_append(src, "    printf(\", \");\n");
        switch (jtype) {
            case JLP7_INT:
                ds_appendf(src,
                    "    printf(\"\\\"" "%s" "\\\": %%lld\", (long long)%s);\n",
                    n, n);
                break;
            case JLP7_FLOAT:
                ds_appendf(src,
                    "    printf(\"\\\"" "%s" "\\\": %%.17g\", (double)%s);\n",
                    n, n);
                break;
            case JLP7_BOOL:
                ds_appendf(src,
                    "    printf(\"\\\"" "%s" "\\\": %%s\", %s ? \"true\" : \"false\");\n",
                    n, n);
                break;
            case JLP7_STRING:
                /* char[] — print as JSON string */
                ds_appendf(src,
                    "    printf(\"\\\"" "%s" "\\\": \\\"%%s\\\"\", %s);\n",
                    n, n);
                break;
        }
        first = 0;
    }

    /* Inherited env vars */
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];
        int redeclared = 0;
        for (int j = 0; j < ndecls; j++)
            if (strcmp(decls[j].name, v->name) == 0) { redeclared = 1; break; }
        if (redeclared) continue;

        if (!first) ds_append(src, "    printf(\", \");\n");
        switch (v->type) {
            case JLP7_INT:
                ds_appendf(src,
                    "    printf(\"\\\"" "%s" "\\\": %%lld\", (long long)%s);\n",
                    v->name, v->name);
                break;
            case JLP7_FLOAT:
                ds_appendf(src,
                    "    printf(\"\\\"" "%s" "\\\": %%.17g\", (double)%s);\n",
                    v->name, v->name);
                break;
            case JLP7_BOOL:
                ds_appendf(src,
                    "    printf(\"\\\"" "%s" "\\\": %%s\", %s ? \"true\" : \"false\");\n",
                    v->name, v->name);
                break;
            case JLP7_STRING:
                ds_appendf(src,
                    "    printf(\"\\\"" "%s" "\\\": \\\"%%s\\\"\", %s);\n",
                    v->name, v->name);
                break;
        }
        first = 0;
    }
#undef EMIT_VAR

    ds_append(src, "    printf(\"}\\n\");\n");
    ds_append(src, "    return 0;\n");
    ds_append(src, "}\n");

    return ds_take(src);
}
