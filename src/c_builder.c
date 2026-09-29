// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "jlp7.h"
#include "dynstr.h"
#include "c_internal.h"

/*
 * c_builder.c — generates a self-contained C program from a JLP7 block.
 *
 * The generated program:
 *   1. #includes, and a small string-printing helper
 *   2. exported struct definitions (see "jlp7:export" below)
 *   3. Preamble: env vars as global declarations
 *   4. User code inside main()
 *   5. printf(__VARS__:{...}) at the end of main()
 *
 * C type mapping:
 *   JLP7_INT    <-> long long   (%lld)
 *   JLP7_FLOAT  <-> double      (%.17g)
 *   JLP7_BOOL   <-> int         (0/1, printed as true/false)
 *   JLP7_STRING <-> char[]      (%s, quoted and escaped in JSON output)
 *   JLP7_ARRAY  <-> double[N]   plus a const long long name_len
 *
 * Structs. C has no run-time type information, so a struct is only
 * exported if you say so:
 *
 *     // jlp7:export
 *     struct Point { double x, y; };
 *     struct Point p = {1.5, 2.5};        // exported as {"x":1.5,"y":2.5}
 *     struct Point q;                     // no initialiser: filled from the
 *                                         // env dict "q", if there is one
 *
 * Supported fields: integer and floating types, bool, char name[N]
 * (string), number name[N] (array), another exported struct, and arrays
 * of exported structs. Pointers are not supported. The definition can be
 * "struct Tag { ... };" or "typedef struct [Tag] { ... } Name;".
 *
 * LIST and DICT values with no struct variable in the block pass through
 * a C block untouched.
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

static int is_c_type_word(const char *w, size_t n) {
    for (int t = 0; C_TYPES[t]; t++)
        if (strlen(C_TYPES[t]) == n && strncmp(w, C_TYPES[t], n) == 0) return 1;
    return 0;
}

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
 *   - struct/union/enum (except structs marked "jlp7:export")
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

                if (ni > 0 && (*after == ' ' || *after == '=' || *after == ';' || *after == '[')) {
                    if ((size_t)*count == cap) {
                        cap *= 2;
                        decls = realloc(decls, sizeof(Jlp7CVarDecl) * cap);
                    }
                    strncpy(decls[*count].type, C_TYPES[t], JLP7_TYPE_MAX - 1);
                    strncpy(decls[*count].name, name, JLP7_NAME_MAX - 1);
                    decls[*count].is_array  = 0;
                    decls[*count].arr_len   = 0;
                    decls[*count].is_string = 0;

                    /* type name[N]  -- fixed-size array declaration.
                     * N must be a literal integer; anything else
                     * (a #define, a variable, no size at all) is not
                     * something we can size the JSON printer loop
                     * with, so it's left as is_array = 0 and simply
                     * won't be exported -- same "best effort" policy
                     * as pointer types below. */
                    while (*after == ' ' || *after == '\t') after++;
                    if (*after == '[') {
                        const char *num = after + 1;
                        char *endptr;
                        long long n = strtoll(num, &endptr, 10);
                        int sized = endptr != num && *endptr == ']' && n > 0;
                        if (strcmp(C_TYPES[t], "char") == 0) {
                            /* char name[] / char name[N] is a C string */
                            decls[*count].is_string = 1;
                            decls[*count].arr_len   = sized ? n : 0;
                        } else if (sized) {
                            decls[*count].is_array = 1;
                            decls[*count].arr_len  = n;
                        }
                    }

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

/* ── Exported structs: scanner ──────────────────────────────────────── */

static const char *sp(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}

static int ident_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* If p is at the word w (followed by a non-identifier char), return the
 * pointer after it, else NULL. */
static const char *word(const char *p, const char *w) {
    size_t n = strlen(w);
    if (strncmp(p, w, n) == 0 && !ident_char(p[n])) return p + n;
    return NULL;
}

/* Is this line (from its first non-blank char) the export marker? */
static int is_export_marker(const char *line, const char *eol) {
    while (line < eol && (*line == ' ' || *line == '\t')) line++;
    if (eol - line < 2 || line[0] != '/' || line[1] != '/') return 0;
    line += 2;
    while (line < eol && (*line == ' ' || *line == '\t')) line++;
    if (strncmp(line, "jlp7:export", 11) != 0) return 0;
    line += 11;
    while (line < eol && (*line == ' ' || *line == '\t' || *line == '\r')) line++;
    return line == eol;
}

/* Copy text with // and slash-star comments removed. Caller frees. */
static char *strip_comments(const char *s, size_t n) {
    char *out = malloc(n + 1), *d = out;
    for (size_t i = 0; i < n; ) {
        if (s[i] == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n && s[i] != '\n') i++;
        } else if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i += 2;
        } else {
            *d++ = s[i++];
        }
    }
    *d = '\0';
    return out;
}

static int find_struct(const Jlp7CScan *sc, const char *ctype, size_t n) {
    for (int i = 0; i < sc->nstructs; i++)
        if (strlen(sc->structs[i].ctype) == n &&
            strncmp(sc->structs[i].ctype, ctype, n) == 0) return i;
    return -1;
}

static int scan_error(Jlp7CScan *sc, const char *fmt, const char *a, const char *b) {
    snprintf(sc->error, sizeof(sc->error), fmt, a ? a : "", b ? b : "");
    return -1;
}

/* Parse the fields between the braces of one definition. */
static int parse_fields(Jlp7CScan *sc, Jlp7CStruct *st, const char *text, size_t n) {
    char *clean = strip_comments(text, n);
    size_t cap = 8;
    st->fields  = malloc(sizeof(Jlp7CField) * cap);
    st->nfields = 0;
    int rc = 0;

    const char *p = clean;
    while (rc == 0) {
        p = sp(p);
        if (!*p) break;

        /* --- base type --- */
        char base[96] = "";
        int  is_struct = 0, sidx = -1;
        const char *q;
        if ((q = word(p, "struct"))) {
            q = sp(q);
            const char *tag = q;
            while (ident_char(*q)) q++;
            char ct[96];
            snprintf(ct, sizeof(ct), "struct %.*s", (int)(q - tag), tag);
            sidx = find_struct(sc, ct, strlen(ct));
            if (sidx < 0) { rc = scan_error(sc, "struct %s: field type '%s' is not an exported struct (define it first, marked jlp7:export)", st->ctype, ct); break; }
            is_struct = 1;
            p = q;
        } else {
            size_t used = 0;
            for (;;) {
                const char *w = sp(p);
                const char *e = w;
                while (ident_char(*e)) e++;
                if (e == w) break;
                if (word(w, "const") || word(w, "volatile")) { p = e; continue; }
                if (is_c_type_word(w, (size_t)(e - w))) {
                    used += (size_t)snprintf(base + used, sizeof(base) - used, "%s%.*s",
                                             used ? " " : "", (int)(e - w), w);
                    p = e;
                    continue;
                }
                break;
            }
            if (!base[0]) {
                /* maybe a typedef'd exported struct */
                const char *w = sp(p), *e = w;
                while (ident_char(*e)) e++;
                sidx = e > w ? find_struct(sc, w, (size_t)(e - w)) : -1;
                if (sidx < 0) {
                    char t[64];
                    snprintf(t, sizeof(t), "%.*s", (int)(e - w), w);
                    rc = scan_error(sc, "struct %s: unsupported field type '%s'", st->ctype, t);
                    break;
                }
                is_struct = 1;
                p = e;
            }
        }

        /* --- declarators: name[, name][N] ... ; --- */
        for (;;) {
            p = sp(p);
            if (*p == '*') { rc = scan_error(sc, "struct %s: pointer fields are not supported (near '%.24s')", st->ctype, p); break; }
            const char *nm = p;
            while (ident_char(*p)) p++;
            if (p == nm) { rc = scan_error(sc, "struct %s: cannot parse a field near '%.20s'", st->ctype, nm); break; }

            Jlp7CField f;
            memset(&f, 0, sizeof(f));
            snprintf(f.name, sizeof(f.name), "%.*s", (int)(p - nm), nm);
            f.sidx = sidx;

            p = sp(p);
            int is_arr = 0;
            if (*p == '[') {
                char *end;
                long long len = strtoll(p + 1, &end, 10);
                if (end == p + 1 || *end != ']' || len <= 0) {
                    rc = scan_error(sc, "struct %s: field '%s' needs a literal array size", st->ctype, f.name);
                    break;
                }
                f.len  = len;
                is_arr = 1;
                p = sp(end + 1);
                if (*p == '[') { rc = scan_error(sc, "struct %s: multi-dimensional field '%s' is not supported", st->ctype, f.name); break; }
            }

            if (is_struct)                       f.kind = is_arr ? CF_STRUCT_ARRAY : CF_STRUCT;
            else if (is_arr && strcmp(base, "char") == 0) f.kind = CF_STRING;
            else if (is_arr)                     f.kind = CF_ARRAY;
            else if (strstr(base, "double") || strstr(base, "float")) f.kind = CF_FLOAT;
            else if (strcmp(base, "bool") == 0 || strcmp(base, "_Bool") == 0) f.kind = CF_BOOL;
            else                                 f.kind = CF_INT;

            if ((size_t)st->nfields == cap) {
                cap *= 2;
                st->fields = realloc(st->fields, sizeof(Jlp7CField) * cap);
            }
            st->fields[st->nfields++] = f;

            if (*p == ',') { p++; continue; }
            if (*p == ';') { p++; break; }
            rc = scan_error(sc, "struct %s: cannot parse a field near '%.20s'", st->ctype, p);
            break;
        }
    }
    free(clean);
    return rc;
}

/* Consume one annotated definition starting at p (just after the marker
 * line). On success returns the pointer just past its ';' and appends
 * the definition to sc->hoisted. NULL on error (sc->error set). */
static const char *parse_definition(Jlp7CScan *sc, DynStr *hoisted, const char *p) {
    const char *start = sp(p);
    const char *q = start;
    int is_typedef = 0;

    const char *r;
    if ((r = word(q, "typedef"))) { is_typedef = 1; q = sp(r); }
    if (!(r = word(q, "struct"))) {
        scan_error(sc, "'// jlp7:export' must be followed by a struct definition, found '%.20s'", start, NULL);
        return NULL;
    }
    q = sp(r);

    char tag[64] = "";
    const char *t0 = q;
    while (ident_char(*q)) q++;
    snprintf(tag, sizeof(tag), "%.*s", (int)(q - t0), t0);
    q = sp(q);
    if (*q != '{') {
        scan_error(sc, "'// jlp7:export' needs a struct definition with a body, found '%.20s'", start, NULL);
        return NULL;
    }

    const char *body = q + 1;
    int depth = 1;
    q++;
    while (*q && depth > 0) {
        if (*q == '{') depth++;
        else if (*q == '}') depth--;
        q++;
    }
    if (depth != 0) { scan_error(sc, "unterminated struct after '// jlp7:export'%s", "", NULL); return NULL; }
    const char *body_end = q - 1;
    if (memchr(body, '{', (size_t)(body_end - body))) {
        scan_error(sc, "nested struct bodies are not supported: define the inner struct separately%s", "", NULL);
        return NULL;
    }

    char ctype[96] = "";
    q = sp(q);
    if (is_typedef) {
        const char *n0 = q;
        while (ident_char(*q)) q++;
        if (q == n0) { scan_error(sc, "typedef struct needs a name after the closing brace%s", "", NULL); return NULL; }
        snprintf(ctype, sizeof(ctype), "%.*s", (int)(q - n0), n0);
        q = sp(q);
    } else {
        if (!tag[0]) { scan_error(sc, "an exported struct needs a tag or a typedef name%s", "", NULL); return NULL; }
        snprintf(ctype, sizeof(ctype), "struct %s", tag);
    }
    if (*q != ';') {
        scan_error(sc, "declare variables of '%s' on their own line; expected ';' after the definition", ctype, NULL);
        return NULL;
    }
    q++;

    Jlp7CStruct st;
    memset(&st, 0, sizeof(st));
    snprintf(st.ctype, sizeof(st.ctype), "%s", ctype);
    sc->structs = realloc(sc->structs, sizeof(Jlp7CStruct) * (size_t)(sc->nstructs + 1));
    if (parse_fields(sc, &st, body, (size_t)(body_end - body)) != 0) {
        free(st.fields);
        return NULL;
    }
    sc->structs[sc->nstructs++] = st;

    ds_appendf(hoisted, "%.*s\n", (int)(q - start), start);
    return q;
}

/* Find "<ctype> name[N]? (= | ;)" declarations of exported structs. */
static void scan_struct_vars(Jlp7CScan *sc) {
    size_t cap = 8;
    sc->svars  = malloc(sizeof(Jlp7CStructVar) * cap);
    sc->nsvars = 0;

    const char *code = sc->body;
    const char *line = code;
    while (*line) {
        const char *ls = line;
        while (*ls == ' ' || *ls == '\t') ls++;

        for (int s = 0; s < sc->nstructs; s++) {
            size_t n = strlen(sc->structs[s].ctype);
            if (strncmp(ls, sc->structs[s].ctype, n) != 0) continue;
            if (ls[n] != ' ' && ls[n] != '\t') continue;

            const char *p = ls + n;
            while (*p == ' ' || *p == '\t') p++;
            const char *nm = p;
            while (ident_char(*p)) p++;
            if (p == nm) break;               /* "struct P *p": not exported */
            Jlp7CStructVar v;
            memset(&v, 0, sizeof(v));
            snprintf(v.name, sizeof(v.name), "%.*s", (int)(p - nm), nm);
            v.sidx = s;

            while (*p == ' ' || *p == '\t') p++;
            if (*p == '[') {
                char *end;
                long long len = strtoll(p + 1, &end, 10);
                if (end == p + 1 || *end != ']' || len <= 0) break;
                v.arr_len = len;
                p = end + 1;
                while (*p == ' ' || *p == '\t') p++;
            }
            if (*p == '=')      v.has_init = 1;
            else if (*p != ';') break;        /* "a, b" etc: best effort */

            /* end of statement: first ';' outside braces/strings */
            int depth = 0, in_str = 0;
            const char *e = p;
            for (; *e; e++) {
                if (in_str) { if (*e == '\\' && e[1]) e++; else if (*e == '"') in_str = 0; continue; }
                if (*e == '"') in_str = 1;
                else if (*e == '{' || *e == '(') depth++;
                else if (*e == '}' || *e == ')') depth--;
                else if (*e == ';' && depth <= 0) break;
            }
            if (*e != ';') break;
            v.stmt_end = (size_t)(e + 1 - code);

            if ((size_t)sc->nsvars == cap) {
                cap *= 2;
                sc->svars = realloc(sc->svars, sizeof(Jlp7CStructVar) * cap);
            }
            sc->svars[sc->nsvars++] = v;
            break;
        }
        while (*line && *line != '\n') line++;
        if (*line) line++;
    }
}

int jlp7_c_scan(const char *code, Jlp7CScan *sc) {
    memset(sc, 0, sizeof(*sc));
    DynStr *body     = ds_new();
    DynStr *hoisted  = ds_new();
    int     rc       = 0;

    const char *line = code;
    while (*line && rc == 0) {
        const char *eol = strchr(line, '\n');
        const char *end = eol ? eol : line + strlen(line);

        if (is_export_marker(line, end)) {
            const char *after = parse_definition(sc, hoisted, eol ? eol + 1 : end);
            if (!after) { rc = -1; break; }
            /* drop the rest of the closing line; keep line count roughly */
            while (*after && *after != '\n') after++;
            ds_append(body, "\n");
            line = *after ? after + 1 : after;
            continue;
        }
        ds_appendf(body, "%.*s\n", (int)(end - line), line);
        line = eol ? eol + 1 : end;
    }

    sc->body    = ds_take(body);
    sc->hoisted = ds_take(hoisted);
    if (rc != 0) return -1;

    sc->decls = jlp7_c_scan_decls(sc->body, &sc->ndecls);
    scan_struct_vars(sc);
    return 0;
}

void jlp7_c_scan_free(Jlp7CScan *sc) {
    for (int i = 0; i < sc->nstructs; i++) free(sc->structs[i].fields);
    free(sc->structs);
    free(sc->body);
    free(sc->hoisted);
    free(sc->decls);
    free(sc->svars);
    memset(sc, 0, sizeof(*sc));
}

/* ── Code generation ────────────────────────────────────────────────── */

static int declared_in_block(const Jlp7CScan *sc, const char *name) {
    for (int j = 0; j < sc->ndecls; j++)
        if (strcmp(sc->decls[j].name, name) == 0) return 1;
    for (int j = 0; j < sc->nsvars; j++)
        if (strcmp(sc->svars[j].name, name) == 0) return 1;
    return 0;
}

/* A number as a C expression. 0 if v is not a number/bool. */
static int emit_num(DynStr *o, const Jlp7Var *v) {
    switch (v->type) {
        case JLP7_INT:   ds_appendf(o, "%lldLL", v->val.i); return 1;
        case JLP7_BOOL:  ds_append(o, v->val.b ? "1" : "0"); return 1;
        case JLP7_FLOAT:
            if (isnan(v->val.f))      ds_append(o, "NAN");
            else if (isinf(v->val.f)) ds_append(o, v->val.f > 0 ? "INFINITY" : "(-INFINITY)");
            else                      ds_appendf(o, "%.17g", v->val.f);
            return 1;
        default: return 0;
    }
}

/* Append "    lhs = <number>;" only if v is a number/bool. */
static void emit_assign_num(DynStr *o, const char *lhs, const Jlp7Var *v) {
    DynStr *e = ds_new();
    if (emit_num(e, v)) ds_appendf(o, "    %s = %s;\n", lhs, e->buf);
    free(ds_take(e));
}

/* Statements that print struct `path` as a JSON object. */
static void emit_struct_json(DynStr *o, const Jlp7CScan *sc, int sidx,
                             const char *path, int depth) {
    const Jlp7CStruct *st = &sc->structs[sidx];
    ds_append(o, "    printf(\"{\");\n");
    for (int i = 0; i < st->nfields; i++) {
        const Jlp7CField *f = &st->fields[i];
        char sub[1024];
        snprintf(sub, sizeof(sub), "%s.%s", path, f->name);

        if (i) ds_append(o, "    printf(\", \");\n");
        ds_appendf(o, "    printf(\"\\\"%s\\\": \");\n", f->name);
        switch (f->kind) {
            case CF_INT:
                ds_appendf(o, "    printf(\"%%lld\", (long long)(%s));\n", sub);
                break;
            case CF_FLOAT:
                ds_appendf(o, "    __jlp7_pdbl((double)(%s));\n", sub);
                break;
            case CF_BOOL:
                ds_appendf(o, "    printf(\"%%s\", (%s) ? \"true\" : \"false\");\n", sub);
                break;
            case CF_STRING:
                ds_appendf(o, "    __jlp7_pstr(%s, %lld);\n", sub, f->len);
                break;
            case CF_ARRAY:
                ds_append(o, "    printf(\"[\");\n");
                ds_appendf(o,
                    "    for (long long __jlp7_i%d = 0; __jlp7_i%d < %lldLL; __jlp7_i%d++) {\n"
                    "        if (__jlp7_i%d) printf(\", \");\n"
                    "        printf(\"%%.17g\", (double)(%s[__jlp7_i%d]));\n"
                    "    }\n",
                    depth, depth, f->len, depth, depth, sub, depth);
                ds_append(o, "    printf(\"]\");\n");
                break;
            case CF_STRUCT:
                emit_struct_json(o, sc, f->sidx, sub, depth + 1);
                break;
            case CF_STRUCT_ARRAY: {
                char elem[1100];
                snprintf(elem, sizeof(elem), "%s[__jlp7_i%d]", sub, depth);
                ds_append(o, "    printf(\"[\");\n");
                ds_appendf(o,
                    "    for (long long __jlp7_i%d = 0; __jlp7_i%d < %lldLL; __jlp7_i%d++) {\n"
                    "        if (__jlp7_i%d) printf(\", \");\n",
                    depth, depth, f->len, depth, depth);
                emit_struct_json(o, sc, f->sidx, elem, depth + 1);
                ds_append(o, "    }\n");
                ds_append(o, "    printf(\"]\");\n");
                break;
            }
        }
    }
    ds_append(o, "    printf(\"}\");\n");
}

/* Statements that copy the env dict into struct `path`. Keys that are
 * missing, or whose type does not fit the field, are skipped. */
static void emit_fill(DynStr *o, const Jlp7CScan *sc, int sidx,
                      const char *path, const Jlp7Var *dict) {
    const Jlp7CStruct *st = &sc->structs[sidx];
    for (int i = 0; i < st->nfields; i++) {
        const Jlp7CField *f = &st->fields[i];
        const Jlp7Var *v = jlp7_dict_get(dict, f->name);
        if (!v) continue;
        char sub[1024];
        snprintf(sub, sizeof(sub), "%s.%s", path, f->name);

        switch (f->kind) {
            case CF_INT:
            case CF_FLOAT:
                emit_assign_num(o, sub, v);
                break;
            case CF_BOOL:
                if (v->type == JLP7_BOOL || v->type == JLP7_INT || v->type == JLP7_FLOAT) {
                    DynStr *e = ds_new();
                    emit_num(e, v);
                    ds_appendf(o, "    %s = (%s) != 0;\n", sub, e->buf);
                    free(ds_take(e));
                }
                break;
            case CF_STRING:
                if (v->type == JLP7_STRING) {
                    ds_appendf(o, "    snprintf(%s, %lld, \"%%s\", \"", sub, f->len);
                    ds_append_c_escaped(o, v->val.s);
                    ds_append(o, "\");\n");
                }
                break;
            case CF_ARRAY:
                for (long long k = 0; k < f->len && (size_t)k < v->arr_len; k++) {
                    char elem[1100];
                    snprintf(elem, sizeof(elem), "%s[%lld]", sub, k);
                    if (v->type == JLP7_ARRAY) {
                        Jlp7Var tmp;
                        memset(&tmp, 0, sizeof(tmp));
                        tmp.type  = JLP7_FLOAT;
                        tmp.val.f = v->val.arr[k];
                        emit_assign_num(o, elem, &tmp);
                    } else if (v->type == JLP7_LIST) {
                        emit_assign_num(o, elem, &v->val.items[k]);
                    }
                }
                break;
            case CF_STRUCT:
                if (v->type == JLP7_DICT) emit_fill(o, sc, f->sidx, sub, v);
                break;
            case CF_STRUCT_ARRAY:
                if (v->type == JLP7_LIST) {
                    for (long long k = 0; k < f->len && (size_t)k < v->arr_len; k++) {
                        char elem[1100];
                        snprintf(elem, sizeof(elem), "%s[%lld]", sub, k);
                        if (v->val.items[k].type == JLP7_DICT)
                            emit_fill(o, sc, f->sidx, elem, &v->val.items[k]);
                    }
                }
                break;
        }
    }
}

/* Fill code for a struct variable declared without an initialiser. */
static void emit_struct_var_fill(DynStr *o, const Jlp7CScan *sc,
                                 const Jlp7CStructVar *sv, const Jlp7Env *env) {
    Jlp7Var *v = jlp7_env_get((Jlp7Env *)env, sv->name);
    if (!v) return;
    if (sv->arr_len == 0) {
        if (v->type == JLP7_DICT) emit_fill(o, sc, sv->sidx, sv->name, v);
    } else if (v->type == JLP7_LIST) {
        for (long long k = 0; k < sv->arr_len && (size_t)k < v->arr_len; k++) {
            char elem[128];
            snprintf(elem, sizeof(elem), "%s[%lld]", sv->name, k);
            if (v->val.items[k].type == JLP7_DICT)
                emit_fill(o, sc, sv->sidx, elem, &v->val.items[k]);
        }
    }
}

/* ── Source assembler ───────────────────────────────────────────────── */

/*
 * Build the complete C program:
 *
 *   #include <stdio.h> ...
 *   static void __jlp7_pstr(...)          // JSON string printer
 *   struct Point { ... };                 // exported definitions
 *
 *   long long x = 6;                      // env vars (preamble)
 *   double pi = 3.14;
 *
 *   int main(void) {
 *       int result = (int)x * 2;          // user code
 *       struct Point q;                   //   (+ fill from env dict "q")
 *
 *       printf("__VARS__:{");             // JSON printer
 *       printf("\"x\": %lld", x);
 *       printf("}\n");
 *       return 0;
 *   }
 */
char *jlp7_c_build_source(const Jlp7CScan *sc, const Jlp7Env *env) {
    DynStr *src = ds_new();

    /* ── Headers and helpers ── */
    ds_append(src,
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n"
        "#include <stdbool.h>\n"
        "#include <math.h>\n"
        "\n"
        "/* JSON string: quotes and control characters escaped. n < 0 means\n"
        " * NUL-terminated; otherwise at most n chars (fixed-size char arrays). */\n"
        "static void __jlp7_pstr(const char *s, long long n) {\n"
        "    putchar('\"');\n"
        "    for (long long i = 0; (n < 0 || i < n) && s[i]; i++) {\n"
        "        unsigned char c = (unsigned char)s[i];\n"
        "        if (c == '\"')       fputs(\"\\\\\\\"\", stdout);\n"
        "        else if (c == '\\\\') fputs(\"\\\\\\\\\", stdout);\n"
        "        else if (c == '\\n') fputs(\"\\\\n\", stdout);\n"
        "        else if (c == '\\r') fputs(\"\\\\r\", stdout);\n"
        "        else if (c == '\\t') fputs(\"\\\\t\", stdout);\n"
        "        else if (c < 32)    printf(\"\\\\u%04x\", c);\n"
        "        else                putchar(c);\n"
        "    }\n"
        "    putchar('\"');\n"
        "}\n"
        "/* A double as JSON that reads back as a float: 3 becomes 3.0. */\n"
        "static void __jlp7_pdbl(double d) {\n"
        "    char b[40];\n"
        "    snprintf(b, sizeof b, \"%.17g\", d);\n"
        "    fputs(b, stdout);\n"
        "    if (!strpbrk(b, \".eEnN\")) fputs(\".0\", stdout);\n"
        "}\n"
        "\n");

    /* ── Exported struct definitions, at file scope ── */
    if (sc->hoisted[0]) {
        ds_append(src, sc->hoisted);
        ds_append(src, "\n");
    }

    /* ── Preamble globals: env vars not redeclared in this block ── */
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];
        if (declared_in_block(sc, v->name)) continue;

        switch (v->type) {
            case JLP7_INT:
                ds_appendf(src, "long long %s = %lldLL;\n", v->name, v->val.i);
                break;
            case JLP7_FLOAT:
                ds_appendf(src, "double %s = ", v->name);
                emit_num(src, v);
                ds_append(src, ";\n");
                break;
            case JLP7_BOOL:
                ds_appendf(src, "int %s = %d;\n", v->name, v->val.b);
                break;
            case JLP7_STRING:
                ds_appendf(src, "char %s[] = \"", v->name);
                ds_append_c_escaped(src, v->val.s);
                ds_append(src, "\";\n");
                break;
            case JLP7_ARRAY:
                ds_appendf(src, "double %s[%zu] = {", v->name,
                           v->arr_len ? v->arr_len : 1);
                for (size_t k = 0; k < v->arr_len; k++) {
                    Jlp7Var tmp;
                    memset(&tmp, 0, sizeof(tmp));
                    tmp.type  = JLP7_FLOAT;
                    tmp.val.f = v->val.arr[k];
                    if (k) ds_append(src, ", ");
                    emit_num(src, &tmp);
                }
                if (!v->arr_len) ds_append(src, "0");
                ds_append(src, "};\n");
                ds_appendf(src, "const long long %s_len = %zuLL;\n",
                           v->name, v->arr_len);
                break;
            case JLP7_LIST:
            case JLP7_DICT:
            case JLP7_NULL:
                break;   /* no C form: passes through the env untouched */
        }
    }
    ds_append(src, "\n");

    /* ── main() + user code (with struct fills after their declarations) ── */
    ds_append(src, "int main(void) {\n");
    size_t pos = 0;
    for (int k = 0; k < sc->nsvars; k++) {
        const Jlp7CStructVar *sv = &sc->svars[k];
        if (sv->has_init) continue;
        DynStr *fill = ds_new();
        emit_struct_var_fill(fill, sc, sv, env);
        if (fill->len) {
            ds_appendf(src, "%.*s", (int)(sv->stmt_end - pos), sc->body + pos);
            ds_append(src, "\n");
            ds_append(src, fill->buf);
            pos = sv->stmt_end;
        }
        free(ds_take(fill));
    }
    ds_append(src, sc->body + pos);
    ds_append(src, "\n");

    /* ── JSON printer ── */
    ds_append(src, "    printf(\"__VARS__:{\");\n");

    int first = 1;

    /* Newly declared vars in this block */
    for (int i = 0; i < sc->ndecls; i++) {
        const Jlp7CVarDecl *d = &sc->decls[i];
        const char *n = d->name;
        if (!first) ds_append(src, "    printf(\", \");\n");

        if (d->is_string) {
            ds_appendf(src, "    printf(\"\\\"%s\\\": \");\n", n);
            ds_appendf(src, "    __jlp7_pstr(%s, %lldLL);\n", n,
                       d->arr_len ? d->arr_len : -1LL);
            first = 0;
            continue;
        }

        if (d->is_array) {
            /* print "name": [v0, v1, ...] by looping at runtime --
             * the loop variable is scoped to this for-statement (C99),
             * so reusing the same name across multiple printed arrays
             * in one generated program is safe. */
            ds_appendf(src, "    printf(\"\\\"%s\\\": [\");\n", n);
            ds_appendf(src,
                "    for (long long __jlp7_i = 0; __jlp7_i < %lldLL; __jlp7_i++) {\n"
                "        if (__jlp7_i) printf(\", \");\n"
                "        printf(\"%%.17g\", (double)%s[__jlp7_i]);\n"
                "    }\n",
                d->arr_len, n);
            ds_append(src, "    printf(\"]\");\n");
            first = 0;
            continue;
        }

        switch (c_type_to_jlp7(d->type)) {
            case JLP7_INT:
                ds_appendf(src,
                    "    printf(\"\\\"%s\\\": %%lld\", (long long)%s);\n", n, n);
                break;
            case JLP7_FLOAT:
                ds_appendf(src, "    printf(\"\\\"%s\\\": \");\n", n);
                ds_appendf(src, "    __jlp7_pdbl((double)%s);\n", n);
                break;
            case JLP7_BOOL:
                ds_appendf(src,
                    "    printf(\"\\\"%s\\\": %%s\", %s ? \"true\" : \"false\");\n", n, n);
                break;
            default:
                break;   /* the scanner only records numeric types here */
        }
        first = 0;
    }

    /* Exported struct variables */
    for (int i = 0; i < sc->nsvars; i++) {
        const Jlp7CStructVar *sv = &sc->svars[i];
        if (!first) ds_append(src, "    printf(\", \");\n");
        ds_appendf(src, "    printf(\"\\\"%s\\\": \");\n", sv->name);
        if (sv->arr_len == 0) {
            emit_struct_json(src, sc, sv->sidx, sv->name, 0);
        } else {
            char elem[128];
            snprintf(elem, sizeof(elem), "%s[__jlp7_s]", sv->name);
            ds_append(src, "    printf(\"[\");\n");
            ds_appendf(src,
                "    for (long long __jlp7_s = 0; __jlp7_s < %lldLL; __jlp7_s++) {\n"
                "        if (__jlp7_s) printf(\", \");\n", sv->arr_len);
            emit_struct_json(src, sc, sv->sidx, elem, 0);
            ds_append(src, "    }\n");
            ds_append(src, "    printf(\"]\");\n");
        }
        first = 0;
    }

    /* Inherited env vars (those with a C form) */
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];
        if (declared_in_block(sc, v->name)) continue;
        if (v->type == JLP7_LIST || v->type == JLP7_DICT || v->type == JLP7_NULL)
            continue;

        if (!first) ds_append(src, "    printf(\", \");\n");
        switch (v->type) {
            case JLP7_INT:
                ds_appendf(src,
                    "    printf(\"\\\"%s\\\": %%lld\", (long long)%s);\n",
                    v->name, v->name);
                break;
            case JLP7_FLOAT:
                ds_appendf(src, "    printf(\"\\\"%s\\\": \");\n", v->name);
                ds_appendf(src, "    __jlp7_pdbl((double)%s);\n", v->name);
                break;
            case JLP7_BOOL:
                ds_appendf(src,
                    "    printf(\"\\\"%s\\\": %%s\", %s ? \"true\" : \"false\");\n",
                    v->name, v->name);
                break;
            case JLP7_STRING:
                ds_appendf(src, "    printf(\"\\\"%s\\\": \");\n", v->name);
                ds_appendf(src, "    __jlp7_pstr(%s, -1);\n", v->name);
                break;
            case JLP7_ARRAY:
                ds_appendf(src, "    printf(\"\\\"%s\\\": [\");\n", v->name);
                ds_appendf(src,
                    "    for (long long __jlp7_i = 0; __jlp7_i < %s_len; __jlp7_i++) {\n"
                    "        if (__jlp7_i) printf(\", \");\n"
                    "        printf(\"%%.17g\", (double)%s[__jlp7_i]);\n"
                    "    }\n",
                    v->name, v->name);
                ds_append(src, "    printf(\"]\");\n");
                break;
            default:
                break;
        }
        first = 0;
    }

    ds_append(src, "    printf(\"}\\n\");\n");
    ds_append(src, "    return 0;\n");
    ds_append(src, "}\n");

    return ds_take(src);
}
