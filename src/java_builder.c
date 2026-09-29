// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include "jlp7.h"
#include "dynstr.h"
#include "java_internal.h"

/*
 * java_builder.c — builds the JShell source fed to the Java runner.
 *
 * Two responsibilities:
 *   1. jlp7_java_scan_decls   — scan a block for variable declarations
 *   2. jlp7_java_build_source — assemble helpers + preamble + user code
 *                               + JSON printer
 *
 * Java -> env goes through one Java helper, __jlp7_json(Object), that
 * writes any String, Number, Boolean, List, Map or array as JSON. So the
 * printer does not care about the declared type of a variable.
 */

/* ── Variable declaration scanner ───────────────────────────────────── */

/* Types we can export. Generic arguments (List<Integer>) and array
 * suffixes (int[]) are accepted after any of these. */
static const char *EXPORTABLE[] = {
    "int", "long", "double", "float", "boolean", "String", "char", "short",
    "byte", "List", "ArrayList", "LinkedList", "Map", "HashMap",
    "LinkedHashMap", "TreeMap", NULL
};

static int is_ident(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '$';
}

static const char *skip_sp(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Match "Type", "Type<...>", "Type[]", "Type<...>[]" at p.
 * Returns the pointer after the type, or NULL if p does not start with an
 * exportable type. The text is copied to out (truncated). */
static const char *scan_type(const char *p, char *out, size_t out_max) {
    const char *start = p;
    while (is_ident(*p) || *p == '.') p++;
    size_t hlen = (size_t)(p - start);
    if (hlen == 0) return NULL;

    /* Ignore a package prefix: java.util.List -> List */
    const char *head = start;
    for (const char *q = start; q < p; q++)
        if (*q == '.') head = q + 1;
    size_t nlen = (size_t)(p - head);

    int known = 0;
    for (int t = 0; EXPORTABLE[t]; t++)
        if (strlen(EXPORTABLE[t]) == nlen && strncmp(head, EXPORTABLE[t], nlen) == 0)
            known = 1;
    if (!known) return NULL;

    if (*p == '<') {                     /* balanced <...> */
        int depth = 0;
        do {
            if (*p == '<') depth++;
            else if (*p == '>') depth--;
            else if (!*p || *p == '\n' || *p == ';' || *p == '=') return NULL;
            p++;
        } while (depth > 0);
    }
    for (;;) {                           /* zero or more [] */
        const char *q = skip_sp(p);
        if (q[0] == '[' && q[1] == ']') p = q + 2; else break;
    }

    size_t n = (size_t)(p - start);
    if (n >= out_max) n = out_max - 1;
    memcpy(out, start, n);
    out[n] = '\0';
    return p;
}

/* Returns a heap-allocated array of VarDecl; *count is set to the length.
 * Caller must free the array. */
Jlp7VarDecl *jlp7_java_scan_decls(const char *code, int *count) {
    size_t       cap   = 32;
    Jlp7VarDecl *decls = malloc(sizeof(Jlp7VarDecl) * cap);
    *count = 0;

    const char *line = code;
    while (*line) {
        line = skip_sp(line);

        char        type[JLP7_TYPE_MAX] = {0};
        const char *after = scan_type(line, type, sizeof(type));
        if (after && (*after == ' ' || *after == '\t')) {
            after = skip_sp(after);

            char   name[JLP7_NAME_MAX] = {0};
            size_t ni = 0;
            while (is_ident(*after) && ni < JLP7_NAME_MAX - 1)
                name[ni++] = *after++;

            const char *rest = skip_sp(after);
            /* "= ..." but not "== ..."; or ";" */
            if (ni > 0 && (*rest == ';' || (*rest == '=' && rest[1] != '='))) {
                if ((size_t)*count == cap) {
                    cap *= 2;
                    decls = realloc(decls, sizeof(Jlp7VarDecl) * cap);
                }
                strncpy(decls[*count].type, type, JLP7_TYPE_MAX - 1);
                decls[*count].type[JLP7_TYPE_MAX - 1] = '\0';
                strncpy(decls[*count].name, name, JLP7_NAME_MAX - 1);
                decls[*count].name[JLP7_NAME_MAX - 1] = '\0';
                (*count)++;
            }
        }
        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
    return decls;
}

/* ── Java literals ──────────────────────────────────────────────────── */

/* Append s as the body of a Java string literal. Non-ASCII becomes
 * \uXXXX (as UTF-16), so the source stays ASCII and no charset is
 * involved. \u000a and friends are written as \n etc.: a raw \u000a
 * inside a literal would end the line. */
static void ds_append_java_escaped(DynStr *d, const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned cp;
        if (*p < 0x80)                { cp = *p++; }
        else if ((*p >> 5) == 0x6 && (p[1] & 0xC0) == 0x80)
            { cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu); p += 2; }
        else if ((*p >> 4) == 0xE && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80)
            { cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu); p += 3; }
        else if ((*p >> 3) == 0x1E && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 &&
                 (p[3] & 0xC0) == 0x80)
            { cp = ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) |
                   ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu); p += 4; }
        else                          { cp = 0xFFFD; p++; }   /* bad byte */

        switch (cp) {
            case '\\': ds_append(d, "\\\\"); break;
            case '"':  ds_append(d, "\\\""); break;
            case '\n': ds_append(d, "\\n");  break;
            case '\r': ds_append(d, "\\r");  break;
            case '\t': ds_append(d, "\\t");  break;
            default:
                if (cp >= 32 && cp < 127) {
                    ds_appendf(d, "%c", (char)cp);
                } else if (cp >= 0x10000) {
                    cp -= 0x10000;
                    ds_appendf(d, "\\u%04x\\u%04x", 0xD800 + (cp >> 10),
                               0xDC00 + (cp & 0x3FF));
                } else {
                    ds_appendf(d, "\\u%04x", cp);
                }
        }
    }
}

static void emit_double(DynStr *d, double f) {
    if (isnan(f))       ds_append(d, "Double.NaN");
    else if (isinf(f))  ds_append(d, f > 0 ? "Double.POSITIVE_INFINITY"
                                           : "Double.NEGATIVE_INFINITY");
    else                ds_appendf(d, "%.17g", f);
}

/* A value as a Java expression of type Object (or a primitive that boxes
 * to the right thing): integers are Long, floats Double, arrays double[],
 * lists List<Object>, dicts Map<String,Object>. */
static void emit_value(DynStr *d, const Jlp7Var *v) {
    switch (v->type) {
        case JLP7_INT:    ds_appendf(d, "%lldL", v->val.i); break;
        case JLP7_FLOAT:
            /* "5" would box to Integer in an Object context: force double */
            ds_append(d, "(double) ");
            emit_double(d, v->val.f);
            break;
        case JLP7_BOOL:   ds_append(d, v->val.b ? "true" : "false"); break;
        case JLP7_NULL:   ds_append(d, "(Object) null"); break;
        case JLP7_STRING:
            ds_append(d, "\"");
            ds_append_java_escaped(d, v->val.s);
            ds_append(d, "\"");
            break;
        case JLP7_ARRAY:
            ds_append(d, "new double[]{");
            for (size_t k = 0; k < v->arr_len; k++) {
                if (k) ds_append(d, ", ");
                emit_double(d, v->val.arr[k]);
            }
            ds_append(d, "}");
            break;
        case JLP7_LIST:
            ds_append(d, "__jlp7_list(");
            for (size_t k = 0; k < v->arr_len; k++) {
                if (k) ds_append(d, ", ");
                emit_value(d, &v->val.items[k]);
            }
            ds_append(d, ")");
            break;
        case JLP7_DICT:
            ds_append(d, "__jlp7_map(");
            for (size_t k = 0; k < v->arr_len; k++) {
                if (k) ds_append(d, ", ");
                ds_append(d, "\"");
                ds_append_java_escaped(d, v->val.items[k].name);
                ds_append(d, "\", ");
                emit_value(d, &v->val.items[k]);
            }
            ds_append(d, ")");
            break;
    }
}

/* Integers become int when they fit, long when they do not. This lets
 * user code write "int doubled = counter * 2;" for a small counter that
 * came from Python. The cost: int arithmetic can overflow where Python's
 * would not, and assigning a long to such a variable needs a cast. */
static void emit_decl(DynStr *d, const Jlp7Var *v) {
    switch (v->type) {
        case JLP7_INT:
            if (v->val.i >= INT_MIN && v->val.i <= INT_MAX)
                ds_appendf(d, "int %s = %lld;\n", v->name, v->val.i);
            else
                ds_appendf(d, "long %s = %lldL;\n", v->name, v->val.i);
            return;
        case JLP7_FLOAT:
            ds_appendf(d, "double %s = ", v->name);
            emit_double(d, v->val.f);
            ds_append(d, ";\n");
            return;
        case JLP7_BOOL:
            ds_appendf(d, "boolean %s = %s;\n", v->name, v->val.b ? "true" : "false");
            return;
        case JLP7_STRING:
            ds_appendf(d, "String %s = \"", v->name);
            ds_append_java_escaped(d, v->val.s);
            ds_append(d, "\";\n");
            return;
        case JLP7_ARRAY:
            ds_appendf(d, "double[] %s = ", v->name);
            emit_value(d, v);
            ds_append(d, ";\n");
            return;
        case JLP7_LIST:
            ds_appendf(d, "List<Object> %s = ", v->name);
            emit_value(d, v);
            ds_append(d, ";\n");
            return;
        case JLP7_DICT:
            ds_appendf(d, "Map<String, Object> %s = ", v->name);
            emit_value(d, v);
            ds_append(d, ";\n");
            return;
        case JLP7_NULL:
            ds_appendf(d, "Object %s = null;\n", v->name);
            return;
    }
}

/* ── Java helpers ───────────────────────────────────────────────────── */

/* Written without backslashes or quotes inside the Java text: BS and Q
 * are built from char codes, so this string needs no escaping layers. */
static const char JAVA_HELPERS[] =
    "List<Object> __jlp7_list(Object... xs) {\n"
    "  List<Object> l = new ArrayList<Object>();\n"
    "  if (xs == null) { l.add(null); return l; }\n"
    "  for (Object x : xs) l.add(x);\n"
    "  return l;\n"
    "}\n"
    "Map<String, Object> __jlp7_map(Object... kv) {\n"
    "  Map<String, Object> m = new LinkedHashMap<String, Object>();\n"
    "  for (int i = 0; i + 1 < kv.length; i += 2) m.put((String) kv[i], kv[i + 1]);\n"
    "  return m;\n"
    "}\n"
    "void __jlp7_str(StringBuilder sb, String s) {\n"
    "  char BS = (char) 92, Q = (char) 34;\n"
    "  sb.append(Q);\n"
    "  for (int i = 0; i < s.length(); i++) {\n"
    "    char c = s.charAt(i);\n"
    "    if (c == Q) sb.append(BS).append(Q);\n"
    "    else if (c == BS) sb.append(BS).append(BS);\n"
    "    else if (c == 10) sb.append(BS).append('n');\n"
    "    else if (c == 13) sb.append(BS).append('r');\n"
    "    else if (c == 9) sb.append(BS).append('t');\n"
    "    else if (c < 32 || c > 126) sb.append(BS).append('u').append(String.format(\"%04x\", (int) c));\n"
    "    else sb.append(c);\n"
    "  }\n"
    "  sb.append(Q);\n"
    "}\n"
    "void __jlp7_w(StringBuilder sb, Object o) {\n"
    "  if (o == null) { sb.append(\"null\"); }\n"
    "  else if (o instanceof String || o instanceof Character) { __jlp7_str(sb, o.toString()); }\n"
    "  else if (o instanceof Boolean || o instanceof Number) { sb.append(o.toString()); }\n"
    "  else if (o instanceof Map) {\n"
    "    sb.append('{');\n"
    "    boolean first = true;\n"
    "    for (Object e : ((Map<?, ?>) o).entrySet()) {\n"
    "      Map.Entry<?, ?> en = (Map.Entry<?, ?>) e;\n"
    "      if (!first) sb.append(\", \");\n"
    "      first = false;\n"
    "      __jlp7_str(sb, String.valueOf(en.getKey()));\n"
    "      sb.append(\": \");\n"
    "      __jlp7_w(sb, en.getValue());\n"
    "    }\n"
    "    sb.append('}');\n"
    "  }\n"
    "  else if (o instanceof Iterable) {\n"
    "    sb.append('[');\n"
    "    boolean first = true;\n"
    "    for (Object x : (Iterable<?>) o) {\n"
    "      if (!first) sb.append(\", \");\n"
    "      first = false;\n"
    "      __jlp7_w(sb, x);\n"
    "    }\n"
    "    sb.append(']');\n"
    "  }\n"
    "  else if (o.getClass().isArray()) {\n"
    "    sb.append('[');\n"
    "    int n = java.lang.reflect.Array.getLength(o);\n"
    "    for (int i = 0; i < n; i++) {\n"
    "      if (i > 0) sb.append(\", \");\n"
    "      __jlp7_w(sb, java.lang.reflect.Array.get(o, i));\n"
    "    }\n"
    "    sb.append(']');\n"
    "  }\n"
    "  else { throw new IllegalArgumentException(\"cannot export value of type \" + o.getClass().getName()); }\n"
    "}\n"
    "String __jlp7_json(Object o) { StringBuilder sb = new StringBuilder(); __jlp7_w(sb, o); return sb.toString(); }\n";

/* ── Source assembler ───────────────────────────────────────────────── */

static int redeclared_in_block(const char *name, const Jlp7VarDecl *decls, int n) {
    for (int j = 0; j < n; j++)
        if (strcmp(decls[j].name, name) == 0) return 1;
    return 0;
}

/*
 * Assemble the full JShell source:
 *   [helpers]
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

    ds_append(src, JAVA_HELPERS);

    /* ── Preamble: inject env vars that aren't redeclared ── */
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];
        if (redeclared_in_block(v->name, decls, ndecls)) continue;
        emit_decl(src, v);
    }

    /* ── User code ── */
    ds_append(src, code);
    ds_append(src, "\n");

    /* ── JSON printer: new decls first, then inherited env vars ── */
    ds_append(src, "System.out.println(\"__VARS__:{\" +\n");
    int first = 1;

    for (int i = 0; i < ndecls; i++) {
        if (!first) ds_append(src, " + \", \" +\n");
        ds_appendf(src, "  \"\\\"%s\\\": \" + __jlp7_json(%s)", decls[i].name, decls[i].name);
        first = 0;
    }
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];
        if (redeclared_in_block(v->name, decls, ndecls)) continue;
        if (!first) ds_append(src, " + \", \" +\n");
        ds_appendf(src, "  \"\\\"%s\\\": \" + __jlp7_json(%s)", v->name, v->name);
        first = 0;
    }
    if (first) ds_append(src, "  \"\"");   /* no variables at all */

    ds_append(src, " + \"}\");\n");
    return ds_take(src);
}
