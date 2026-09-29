// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "jlp7.h"
#include "java_internal.h"

static int passed = 0;
static int failed = 0;

#define ASSERT(cond, msg) do {                          \
    if (cond) { printf("  ✓ %s\n", msg); passed++; }   \
    else       { printf("  ✗ %s\n", msg); failed++; }  \
} while(0)

/* ── Parser ─────────────────────────────────────────────────────────── */

static void test_parser_basic(void) {
    printf("\n── Parser: basic split ──\n");
    const char *src =
        "int x = 5;\n"
        "/p\n"
        "x = x + 1\n"
        "p/\n"
        "int z = x;\n";

    Jlp7Block *b = jlp7_parse(src);
    ASSERT(b != NULL,                              "got blocks");
    ASSERT(b->type == JLP7_BLOCK_FOREIGN,          "block 0: foreign");
    ASSERT(b->next && b->next->type == JLP7_BLOCK_PYTHON, "block 1: python");
    ASSERT(b->next->next && b->next->next->type == JLP7_BLOCK_FOREIGN, "block 2: foreign");
    ASSERT(b->next->next->next == NULL,            "no block 3");
    jlp7_blocks_free(b);
}

static void test_parser_multiple_py(void) {
    printf("\n── Parser: multiple python blocks ──\n");
    const char *src =
        "int x = 1;\n"
        "/p\nx = 2\np/\n"
        "int y = 3;\n"
        "/p\ny = y + x\np/\n"
        "int z = 0;\n";

    Jlp7Block *b = jlp7_parse(src);
    int count = 0;
    Jlp7BlockType expected[] = {
        JLP7_BLOCK_FOREIGN, JLP7_BLOCK_PYTHON,
        JLP7_BLOCK_FOREIGN, JLP7_BLOCK_PYTHON,
        JLP7_BLOCK_FOREIGN
    };
    Jlp7Block *cur = b;
    while (cur) { count++; cur = cur->next; }
    ASSERT(count == 5, "5 blocks total");

    cur = b;
    int ok = 1;
    for (int i = 0; i < 5 && cur; i++, cur = cur->next)
        if (cur->type != expected[i]) ok = 0;
    ASSERT(ok, "block types in correct order");
    jlp7_blocks_free(b);
}

static void test_parser_no_python(void) {
    printf("\n── Parser: no python blocks ──\n");
    const char *src = "int x = 5;\nint y = 10;\n";
    Jlp7Block *b = jlp7_parse(src);
    ASSERT(b != NULL,                      "got a block");
    ASSERT(b->type == JLP7_BLOCK_FOREIGN,  "it is foreign");
    ASSERT(b->next == NULL,                "only one block");
    jlp7_blocks_free(b);
}

/* ── Env store ──────────────────────────────────────────────────────── */

static void test_env(void) {
    printf("\n── Env store ──\n");
    Jlp7Env *env = jlp7_env_new();

    jlp7_env_set_int(env,   "x",    42);
    jlp7_env_set_float(env, "pi",   3.14);
    jlp7_env_set_bool(env,  "flag", 1);
    jlp7_env_set_str(env,   "name", "jlp7");

    Jlp7Var *v;
    v = jlp7_env_get(env, "x");
    ASSERT(v && v->type == JLP7_INT && v->val.i == 42,    "int stored");
    v = jlp7_env_get(env, "pi");
    ASSERT(v && v->type == JLP7_FLOAT && v->val.f == 3.14,"float stored");
    v = jlp7_env_get(env, "flag");
    ASSERT(v && v->type == JLP7_BOOL && v->val.b == 1,    "bool stored");
    v = jlp7_env_get(env, "name");
    ASSERT(v && v->type == JLP7_STRING &&
           strcmp(v->val.s, "jlp7") == 0,                  "string stored");

    /* Overwrite */
    jlp7_env_set_int(env, "x", 99);
    v = jlp7_env_get(env, "x");
    ASSERT(v && v->val.i == 99, "int overwritten");

    /* String overwrite (no leak) */
    jlp7_env_set_str(env, "name", "Jean-Luc");
    v = jlp7_env_get(env, "name");
    ASSERT(v && strcmp(v->val.s, "Jean-Luc") == 0, "string overwritten");

    /* Non-existent key */
    ASSERT(jlp7_env_get(env, "nope") == NULL, "missing key returns NULL");

    jlp7_env_free(env);
}

/* ── Python runner ──────────────────────────────────────────────────── */

static void test_python_arithmetic(void) {
    printf("\n── Python runner: arithmetic ──\n");
    Jlp7Env *env = jlp7_env_new();
    jlp7_env_set_int(env, "x", 5);
    jlp7_env_set_int(env, "y", 10);

    int rc = jlp7_run_python("x = x + 1\nresult = x * y\n", env);
    ASSERT(rc == 0, "ran without error");

    Jlp7Var *v;
    v = jlp7_env_get(env, "x");
    ASSERT(v && v->val.i == 6,  "x mutated to 6");
    v = jlp7_env_get(env, "result");
    ASSERT(v && v->val.i == 60, "result = 60");

    jlp7_env_free(env);
}

static void test_python_strings(void) {
    printf("\n── Python runner: strings ──\n");
    Jlp7Env *env = jlp7_env_new();
    jlp7_env_set_str(env, "greeting", "hello");

    int rc = jlp7_run_python("greeting = greeting.upper() + ' WORLD'\n", env);
    ASSERT(rc == 0, "ran without error");

    Jlp7Var *v = jlp7_env_get(env, "greeting");
    ASSERT(v && strcmp(v->val.s, "HELLO WORLD") == 0, "string mutated");

    jlp7_env_free(env);
}

static void test_python_bool(void) {
    printf("\n── Python runner: booleans ──\n");
    Jlp7Env *env = jlp7_env_new();
    jlp7_env_set_bool(env, "flag", 0);

    int rc = jlp7_run_python("flag = not flag\n", env);
    ASSERT(rc == 0, "ran without error");

    Jlp7Var *v = jlp7_env_get(env, "flag");
    ASSERT(v && v->type == JLP7_BOOL && v->val.b == 1, "bool flipped");

    jlp7_env_free(env);
}

static void test_python_error(void) {
    printf("\n── Python runner: error handling ──\n");
    Jlp7Env *env = jlp7_env_new();

    /* Syntax error */
    int rc = jlp7_run_python("def (:\n", env);
    ASSERT(rc != 0, "syntax error returns -1");

    /* Runtime error */
    rc = jlp7_run_python("x = 1 / 0\n", env);
    ASSERT(rc != 0, "runtime error returns -1");

    jlp7_env_free(env);
}

/* ── Structured errors & transactional env ──────────────────────────── */

static void test_error_struct(void) {
    printf("\n── Errors: structured Jlp7Error ──\n");
    Jlp7Config cfg = jlp7_default_config("c");
    Jlp7Env   *env = jlp7_env_new();
    Jlp7Error  err;

    /* Runtime error on line 4 of the original source, in block 1 */
    int rc = jlp7_exec_ex("long long x = 1;\n/p\nx = 2\ny = 1 / 0\np/\n",
                          &cfg, env, &err);
    ASSERT(rc == -1,                                  "runtime error returns -1");
    ASSERT(err.kind == JLP7_ERR_PY_RUNTIME,           "kind = python-runtime");
    ASSERT(err.exc_type && strcmp(err.exc_type, "ZeroDivisionError") == 0,
                                                      "exc_type captured");
    ASSERT(err.message && strstr(err.message, "division"), "message captured");
    ASSERT(err.traceback && strstr(err.traceback, "Traceback"), "traceback captured");
    ASSERT(err.line == 4,                             "line maps to original source");
    ASSERT(err.block_index == 1,                      "block_index = 1");
    jlp7_error_clear(&err);

    /* Syntax error */
    memset(&err, 0, sizeof(err));
    rc = jlp7_exec_ex("long long x = 1;\n\n/p\nx = = 2\np/\n", &cfg, env, &err);
    ASSERT(rc == -1 && err.kind == JLP7_ERR_PY_COMPILE, "kind = python-compile");
    ASSERT(err.exc_type && strcmp(err.exc_type, "SyntaxError") == 0, "SyntaxError");
    ASSERT(err.line == 4,                             "syntax error line maps");
    jlp7_error_clear(&err);

    /* sys.exit must not kill the host process */
    memset(&err, 0, sizeof(err));
    rc = jlp7_exec_ex("/p\nimport sys\nsys.exit(3)\np/\n", &cfg, env, &err);
    ASSERT(rc == -1 && err.exc_type && strcmp(err.exc_type, "SystemExit") == 0,
                                                      "sys.exit is an error, not an exit");
    jlp7_error_clear(&err);

    /* allowpy=0 */
    cfg.allowpy = 0;
    memset(&err, 0, sizeof(err));
    rc = jlp7_exec_ex("/p\nx = 1\np/\n", &cfg, env, &err);
    ASSERT(rc == -1 && err.kind == JLP7_ERR_CONFIG,   "allowpy=0 -> config error");
    jlp7_error_clear(&err);

    jlp7_env_free(env);
}

static void test_transactional_env(void) {
    printf("\n── Errors: env is all-or-nothing ──\n");
    Jlp7Config cfg = jlp7_default_config("c");
    Jlp7Env   *env = jlp7_env_new();
    jlp7_env_set_int(env, "x", 1);

    /* Block 0 succeeds and changes x; block 1 fails. x must be back to 1
     * and "y" must not exist. */
    int rc = jlp7_exec("/p\nx = 100\ny = 5\np/\n/p\nraise ValueError('boom')\np/\n",
                       &cfg, env);
    ASSERT(rc == -1, "exec failed");
    Jlp7Var *v = jlp7_env_get(env, "x");
    ASSERT(v && v->val.i == 1,           "x rolled back to 1");
    ASSERT(jlp7_env_get(env, "y") == NULL, "y from earlier block rolled back");

    /* A successful run still commits */
    rc = jlp7_exec("/p\nx = 7\np/\n", &cfg, env);
    v = jlp7_env_get(env, "x");
    ASSERT(rc == 0 && v && v->val.i == 7, "success commits");

    jlp7_env_free(env);
}

static void test_marshal_errors(void) {
    printf("\n── Errors: marshalling ──\n");
    Jlp7Env  *env = jlp7_env_new();
    Jlp7Error err;
    memset(&err, 0, sizeof(err));

    /* Integer outside long long: always an error, never a silent -1 */
    int rc = jlp7_run_python_ex("big = 2**80\n", env, 1, 0, &err);
    ASSERT(rc == -1 && err.kind == JLP7_ERR_MARSHAL, "int overflow is a marshal error");
    ASSERT(err.message && strstr(err.message, "big"), "message names the variable");
    ASSERT(jlp7_env_get(env, "big") == NULL,          "nothing exported");
    jlp7_error_clear(&err);

    /* set: no representation, skipped by default ... */
    rc = jlp7_run_python_ex("d = {1, 2}\nn = 3\n", env, 1, 0, &err);
    ASSERT(rc == 0 && jlp7_env_get(env, "d") == NULL && jlp7_env_get(env, "n"),
                                                      "non-strict skips set, keeps n");

    /* ... and an error when strict, with no partial export */
    rc = jlp7_run_python_ex("m = 9\nd = {1, 2}\n", env, 1, 1, &err);
    ASSERT(rc == -1 && err.kind == JLP7_ERR_MARSHAL, "strict: set is a marshal error");
    ASSERT(jlp7_env_get(env, "m") == NULL,            "strict: no partial export");
    jlp7_error_clear(&err);

    jlp7_env_free(env);
}

/* ── Nested values ───────────────────────────────────────────────────── */

static void test_json_nested(void) {
    printf("\n── Nested: JSON wire format ──\n");
    Jlp7Env *env = jlp7_env_new();
    char why[96] = "";

    int rc = jlp7_java_parse_vars(
        "{\"n\": 3, \"f\": 2.5, \"e\": 1E3, \"t\": true, \"z\": null,"
        " \"s\": \"a\\\"b\\n\\u00e9\\ud83d\\ude00\","
        " \"nums\": [1, 2.5, 3], \"none\": [],"
        " \"mix\": [1, \"two\", [3, 4], {\"k\": false}],"
        " \"cfg\": {\"name\": \"x\", \"pos\": {\"x\": 1, \"y\": 2}, \"w\": [0.5, 1.5]},"
        " \"nan\": NaN, \"inf\": Infinity, \"ninf\": -Infinity, \"cnan\": -nan,"
        " \"big\": 99999999999999999999}",
        env, why, sizeof(why));
    ASSERT(rc == 0, "nested JSON parses");

    Jlp7Var *v;
    v = jlp7_env_get(env, "n");   ASSERT(v && v->type == JLP7_INT && v->val.i == 3, "int");
    v = jlp7_env_get(env, "f");   ASSERT(v && v->type == JLP7_FLOAT && v->val.f == 2.5, "float");
    v = jlp7_env_get(env, "e");   ASSERT(v && v->type == JLP7_FLOAT && v->val.f == 1000.0, "1E3 is a float");
    v = jlp7_env_get(env, "t");   ASSERT(v && v->type == JLP7_BOOL && v->val.b == 1, "bool");
    v = jlp7_env_get(env, "z");   ASSERT(v && v->type == JLP7_NULL, "null");
    v = jlp7_env_get(env, "s");
    ASSERT(v && v->type == JLP7_STRING &&
           strcmp(v->val.s, "a\"b\n\xc3\xa9\xf0\x9f\x98\x80") == 0,
           "string escapes, \\u00e9 and a surrogate pair");
    v = jlp7_env_get(env, "nums");
    ASSERT(v && v->type == JLP7_ARRAY && v->arr_len == 3 && v->val.arr[1] == 2.5,
           "numeric array stays a flat ARRAY");
    v = jlp7_env_get(env, "none");
    ASSERT(v && v->type == JLP7_ARRAY && v->arr_len == 0, "[] is an empty ARRAY");

    v = jlp7_env_get(env, "mix");
    ASSERT(v && v->type == JLP7_LIST && v->arr_len == 4, "mixed array is a LIST");
    Jlp7Var *it = jlp7_list_at(v, 1);
    ASSERT(it && it->type == JLP7_STRING && strcmp(it->val.s, "two") == 0, "list item 1");
    it = jlp7_list_at(v, 2);
    ASSERT(it && it->type == JLP7_ARRAY && it->arr_len == 2, "list item 2 is an ARRAY");
    it = jlp7_list_at(v, 3);
    Jlp7Var *k = jlp7_dict_get(it, "k");
    ASSERT(it && it->type == JLP7_DICT && k && k->type == JLP7_BOOL && k->val.b == 0,
           "list item 3 is a DICT");

    v = jlp7_env_get(env, "cfg");
    Jlp7Var *pos = jlp7_dict_get(v, "pos");
    Jlp7Var *py  = jlp7_dict_get(pos, "y");
    ASSERT(v && v->type == JLP7_DICT && v->arr_len == 3, "dict with 3 keys");
    ASSERT(py && py->type == JLP7_INT && py->val.i == 2, "dict.pos.y == 2");

    v = jlp7_env_get(env, "nan");  ASSERT(v && v->type == JLP7_FLOAT && isnan(v->val.f), "NaN");
    v = jlp7_env_get(env, "inf");  ASSERT(v && isinf(v->val.f) && v->val.f > 0, "Infinity");
    v = jlp7_env_get(env, "ninf"); ASSERT(v && isinf(v->val.f) && v->val.f < 0, "-Infinity");
    v = jlp7_env_get(env, "cnan"); ASSERT(v && isnan(v->val.f), "printf-style -nan");
    v = jlp7_env_get(env, "big");  ASSERT(v && v->type == JLP7_FLOAT, "integer too big for long long -> float");

    /* Malformed input: error, and env is untouched */
    size_t before = env->count;
    rc = jlp7_java_parse_vars("{\"a\": 1, \"b\": [1, 2", env, why, sizeof(why));
    ASSERT(rc == -1 && why[0], "truncated JSON is rejected with a reason");
    ASSERT(env->count == before && jlp7_env_get(env, "a") == NULL,
           "malformed JSON leaves env untouched");
    rc = jlp7_java_parse_vars("{\"a\": @}", env, why, sizeof(why));
    ASSERT(rc == -1, "bad token is rejected");

    /* Depth limit: 200 nested arrays */
    char deep[1024];
    size_t n = 0;
    n += (size_t)snprintf(deep + n, sizeof(deep) - n, "{\"d\": ");
    for (int i = 0; i < 200; i++) deep[n++] = '[';
    for (int i = 0; i < 200; i++) deep[n++] = ']';
    n += (size_t)snprintf(deep + n, sizeof(deep) - n, "}");
    rc = jlp7_java_parse_vars(deep, env, why, sizeof(why));
    ASSERT(rc == -1 && strstr(why, "deep"), "nesting deeper than 64 is rejected");

    jlp7_env_free(env);
}

static void test_value_api(void) {
    printf("\n── Nested: value API and deep copy ──\n");
    Jlp7Env *env = jlp7_env_new();
    Jlp7Var *cfg = jlp7_env_slot(env, "cfg");
    jlp7_var_set_dict(cfg);
    jlp7_var_set_str(jlp7_dict_put(cfg, "name"), "jlp7");
    Jlp7Var *tags = jlp7_dict_put(cfg, "tags");
    jlp7_var_set_list(tags);
    for (int i = 0; i < 100; i++) jlp7_var_set_int(jlp7_list_push(tags), i);
    ASSERT(tags->arr_len == 100, "100 pushes");
    Jlp7Var *last = jlp7_list_at(tags, 99);
    ASSERT(last && last->val.i == 99, "list_at(99)");

    Jlp7Env *copy = jlp7_env_clone(env);
    jlp7_var_set_str(jlp7_dict_put(jlp7_env_get(env, "cfg"), "name"), "changed");
    Jlp7Var *cname = jlp7_dict_get(jlp7_env_get(copy, "cfg"), "name");
    ASSERT(cname && strcmp(cname->val.s, "jlp7") == 0, "clone is independent (deep)");
    Jlp7Var *ctags = jlp7_dict_get(jlp7_env_get(copy, "cfg"), "tags");
    ASSERT(ctags && ctags->arr_len == 100 && jlp7_list_at(ctags, 42)->val.i == 42,
           "clone keeps the whole list");

    /* Overwriting with a scalar frees the tree (checked by ASan) */
    jlp7_env_set_int(env, "cfg", 1);
    ASSERT(jlp7_env_get(env, "cfg")->type == JLP7_INT, "container replaced by int");

    /* put on an existing key resets it */
    Jlp7Var *c2 = jlp7_env_slot(copy, "cfg");
    ASSERT(c2->type == JLP7_NULL, "env_slot clears old content");

    jlp7_env_free(copy);
    jlp7_env_free(env);
}

static void test_python_nested(void) {
    printf("\n── Nested: Python blocks ──\n");
    Jlp7Env  *env = jlp7_env_new();
    Jlp7Error err;
    memset(&err, 0, sizeof(err));

    /* Build a nested value in C, mutate it in Python. */
    Jlp7Var *cfg = jlp7_env_slot(env, "cfg");
    jlp7_var_set_dict(cfg);
    jlp7_var_set_str(jlp7_dict_put(cfg, "name"), "run1");
    Jlp7Var *layers = jlp7_dict_put(cfg, "layers");
    jlp7_var_set_list(layers);
    for (int i = 0; i < 3; i++) {
        Jlp7Var *l = jlp7_list_push(layers);
        jlp7_var_set_dict(l);
        jlp7_var_set_int(jlp7_dict_put(l, "units"), 8 << i);
    }
    Jlp7Var *none = jlp7_dict_put(cfg, "note");
    jlp7_var_set_null(none);

    int rc = jlp7_run_python_ex(
        "cfg['name'] = cfg['name'].upper()\n"
        "cfg['layers'].append({'units': 64})\n"
        "cfg['total'] = sum(l['units'] for l in cfg['layers'])\n"
        "cfg['note'] = 'was None: %s' % (cfg['note'] is None)\n"
        "lr = [0.1, 0.01]\n"
        "hist = [{'epoch': i, 'loss': 1.0 / (i + 1), 'tags': ['a', 'b']} for i in range(3)]\n"
        "empty = []\n"
        "tup = ('x', 1, None)\n"
        "matrix = [[1, 2], [3, 4]]\n",
        env, 1, 1, &err);
    ASSERT(rc == 0, "python ran on a nested env");
    if (rc != 0) fprintf(stderr, "%s\n", err.message);

    cfg = jlp7_env_get(env, "cfg");
    ASSERT(strcmp(jlp7_dict_get(cfg, "name")->val.s, "RUN1") == 0, "dict value updated");
    Jlp7Var *ly = jlp7_dict_get(cfg, "layers");
    ASSERT(ly && ly->arr_len == 4 && jlp7_dict_get(jlp7_list_at(ly, 3), "units")->val.i == 64,
           "list item appended in Python");
    ASSERT(jlp7_dict_get(cfg, "total")->val.i == 8 + 16 + 32 + 64, "total computed");
    ASSERT(strcmp(jlp7_dict_get(cfg, "note")->val.s, "was None: True") == 0,
           "None arrived as None, came back as str");

    Jlp7Var *lr = jlp7_env_get(env, "lr");
    ASSERT(lr && lr->type == JLP7_ARRAY && lr->arr_len == 2, "numeric list -> ARRAY (unchanged)");
    Jlp7Var *h = jlp7_env_get(env, "hist");
    ASSERT(h && h->type == JLP7_LIST && h->arr_len == 3, "list of dicts -> LIST");
    Jlp7Var *h2 = jlp7_list_at(h, 2);
    ASSERT(jlp7_dict_get(h2, "epoch")->val.i == 2, "hist[2].epoch");
    ASSERT(jlp7_dict_get(h2, "tags")->type == JLP7_LIST &&
           jlp7_dict_get(h2, "tags")->arr_len == 2, "hist[2].tags is a LIST of str");
    Jlp7Var *e = jlp7_env_get(env, "empty");
    ASSERT(e && e->arr_len == 0, "empty list is exported");
    Jlp7Var *t = jlp7_env_get(env, "tup");
    ASSERT(t && t->type == JLP7_LIST && t->arr_len == 3 &&
           jlp7_list_at(t, 2)->type == JLP7_NULL, "tuple with None -> LIST");
    Jlp7Var *m = jlp7_env_get(env, "matrix");
    ASSERT(m && m->type == JLP7_ARRAY && m->arr_len == 4,
           "numeric 2D list still flattens to a flat ARRAY");

    /* Things that cannot be represented */
    rc = jlp7_run_python_ex("a = []\na.append(a)\nok = 1\n", env, 1, 0, &err);
    ASSERT(rc == 0 && jlp7_env_get(env, "a") == NULL && jlp7_env_get(env, "ok"),
           "self-containing list: skipped, no crash");
    rc = jlp7_run_python_ex("a = []\na.append(a)\n", env, 1, 1, &err);
    ASSERT(rc == -1 && err.kind == JLP7_ERR_MARSHAL, "self-containing list: strict error");
    jlp7_error_clear(&err);
    rc = jlp7_run_python_ex("x = 'leaf'\nfor _ in range(100): x = [x]\n", env, 1, 1, &err);
    ASSERT(rc == -1 && err.kind == JLP7_ERR_MARSHAL, "100-deep nesting: strict error");
    jlp7_error_clear(&err);
    rc = jlp7_run_python_ex("d = {1: 'a'}\n", env, 1, 1, &err);
    ASSERT(rc == -1 && err.message && strstr(err.message, "non-string key"),
           "dict with int key: strict error");
    jlp7_error_clear(&err);
    rc = jlp7_run_python_ex("d = {'ok': 1, 'bad': {1, 2}}\ne = 5\n", env, 1, 0, &err);
    ASSERT(rc == 0 && jlp7_env_get(env, "d") == NULL && jlp7_env_get(env, "e"),
           "container holding a set: exported whole or not at all");
    rc = jlp7_run_python_ex("d = {'big': 2**70}\n", env, 1, 0, &err);
    ASSERT(rc == -1 && err.kind == JLP7_ERR_MARSHAL, "int overflow inside a dict is an error");
    jlp7_error_clear(&err);

    jlp7_env_free(env);
}

static void test_python_dedent(void) {
    printf("\n── Python runner: indented blocks ──\n");
    Jlp7Env  *env = jlp7_env_new();
    Jlp7Error err;
    memset(&err, 0, sizeof(err));
    jlp7_env_set_int(env, "x", 5);

    int rc = jlp7_run_python_ex(
        "    x = x * 2\n"
        "    if x > 5:\n"
        "        label = 'big'\n"
        "\n"
        "    y = x + 1\n", env, 1, 0, &err);
    ASSERT(rc == 0, "block indented by 4 runs");
    Jlp7Var *v = jlp7_env_get(env, "y");
    ASSERT(v && v->val.i == 11, "code ran correctly after dedent");
    ASSERT(jlp7_env_get(env, "label") != NULL, "nested indentation kept");

    /* line numbers still refer to the original source */
    Jlp7Config cfg = jlp7_default_config("c");
    rc = jlp7_exec_ex("long long a = 1;\n"
                      "\t/p\n"
                      "\t\ta = 2\n"
                      "\t\tb = 1 / 0\n"
                      "\tp/\n", &cfg, env, &err);
    ASSERT(rc == -1 && err.line == 4, "error line is still 4 after dedent");
    jlp7_error_clear(&err);

    /* less-indented later line: nothing to remove, normal behaviour */
    rc = jlp7_run_python_ex("a = 1\n    b = 2\n", env, 1, 0, &err);
    ASSERT(rc == -1 && err.kind == JLP7_ERR_PY_COMPILE, "real indentation errors still reported");
    jlp7_error_clear(&err);

    jlp7_env_free(env);
}

/* ── allowpy guard ──────────────────────────────────────────────────── */

static void test_allowpy_false(void) {
    printf("\n── allowpy=0 guard ──\n");
    Jlp7Config cfg = jlp7_default_config("java");
    cfg.allowpy = 0;
    Jlp7Env *env = jlp7_env_new();

    int rc = jlp7_exec("/p\nx = 1\np/\n", &cfg, env);
    ASSERT(rc != 0, "allowpy=0 rejects /p...p/ block");

    jlp7_env_free(env);
}

/* ── Java integration (requires jshell) ─────────────────────────────── */

static int has_jshell(void) {
    FILE *f = popen("which jshell 2>/dev/null", "r");
    if (!f) return 0;
    char buf[8];
    int found = fgets(buf, sizeof(buf), f) && buf[0] != '\0';
    pclose(f);
    return found;
}

static void test_java_basic(void) {
    printf("\n── Java: basic variable round-trip ──\n");
    if (!has_jshell()) { printf("  ⚠ jshell not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("java");
    Jlp7Env   *env = jlp7_env_new();

    int rc = jlp7_exec(
        "int x = 5;\n"
        "int y = 10;\n"
        "/p\n"
        "x = x + 1\n"
        "result = x * y\n"
        "p/\n"
        "System.out.println(\"result = \" + result);\n",
        &cfg, env);

    ASSERT(rc == 0, "exec returned 0");
    Jlp7Var *v;
    v = jlp7_env_get(env, "x");
    ASSERT(v && v->val.i == 6,  "x = 6 after python block");
    v = jlp7_env_get(env, "result");
    ASSERT(v && v->val.i == 60, "result = 60");

    jlp7_env_free(env);
}

static void test_java_strings(void) {
    printf("\n── Java: string round-trip ──\n");
    if (!has_jshell()) { printf("  ⚠ jshell not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("java");
    Jlp7Env   *env = jlp7_env_new();

    int rc = jlp7_exec(
        "String greeting = \"hello\";\n"
        "/p\n"
        "greeting = greeting.upper() + ' WORLD'\n"
        "p/\n"
        "System.out.println(greeting);\n",
        &cfg, env);

    ASSERT(rc == 0, "exec returned 0");
    Jlp7Var *v = jlp7_env_get(env, "greeting");
    ASSERT(v && strcmp(v->val.s, "HELLO WORLD") == 0, "string mutated correctly");

    jlp7_env_free(env);
}

static void test_java_multi_block(void) {
    printf("\n── Java: multi-block pipeline ──\n");
    if (!has_jshell()) { printf("  ⚠ jshell not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("java");
    Jlp7Env   *env = jlp7_env_new();

    int rc = jlp7_exec(
        "int counter = 0;\n"
        "/p\n"
        "counter = counter + 10\n"
        "p/\n"
        "int doubled = counter * 2;\n"
        "/p\n"
        "final_val = doubled + 1\n"
        "p/\n",
        &cfg, env);

    ASSERT(rc == 0, "exec returned 0");
    Jlp7Var *v;
    v = jlp7_env_get(env, "counter");
    ASSERT(v && v->val.i == 10,  "counter = 10");
    v = jlp7_env_get(env, "doubled");
    ASSERT(v && v->val.i == 20,  "doubled = 20");
    v = jlp7_env_get(env, "final_val");
    ASSERT(v && v->val.i == 21,  "final_val = 21");

    jlp7_env_free(env);
}

static void test_java_nested(void) {
    printf("\n── Java: nested values, both directions ──\n");
    if (!has_jshell()) { printf("  ⚠ jshell not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("java");
    Jlp7Env   *env = jlp7_env_new();
    Jlp7Error  err;

    /* Python builds nested data; Java reads and changes it. */
    int rc = jlp7_exec_ex(
        "/p\n"
        "cfg = {'name': 'a\"b', 'layers': [{'units': 8}, {'units': 16}],\n"
        "       'opt': None, 'msg': 'h\\u00e9llo \\U0001F600\\n'}\n"
        "lr = [0.1, 0.01]\n"
        "small = 5\n"
        "big = 3000000000\n"
        "flag = True\n"
        "p/\n"
        "List<Object> layers = (List<Object>) cfg.get(\"layers\");\n"
        "long total = 0;\n"
        "for (Object o : layers) { total += (Long) ((Map<String, Object>) o).get(\"units\"); }\n"
        "cfg.put(\"total\", total);\n"
        "boolean optWasNull = cfg.get(\"opt\") == null;\n"
        "lr[0] = lr[0] * 2;\n"
        "int twice = small * 2;\n"
        "long bigger = big + 1;\n",
        &cfg, env, &err);
    ASSERT(rc == 0, "python -> java -> env succeeded");
    if (rc != 0) { fprintf(stderr, "%s\n", err.message ? err.message : "?"); jlp7_error_clear(&err); }

    Jlp7Var *c = jlp7_env_get(env, "cfg");
    ASSERT(c && c->type == JLP7_DICT, "cfg is still a DICT");
    Jlp7Var *nm = jlp7_dict_get(c, "name");
    ASSERT(nm && strcmp(nm->val.s, "a\"b") == 0, "string with a quote survives");
    Jlp7Var *msg = jlp7_dict_get(c, "msg");
    ASSERT(msg && strcmp(msg->val.s, "h\xc3\xa9llo \xf0\x9f\x98\x80\n") == 0,
           "non-ASCII, emoji and newline survive");
    Jlp7Var *tot = jlp7_dict_get(c, "total");
    ASSERT(tot && tot->type == JLP7_INT && tot->val.i == 24, "Java added cfg.total = 24");
    Jlp7Var *opt = jlp7_dict_get(c, "opt");
    ASSERT(opt && opt->type == JLP7_NULL, "null stays null");
    Jlp7Var *lay = jlp7_dict_get(c, "layers");
    ASSERT(lay && lay->type == JLP7_LIST && lay->arr_len == 2, "layers still a LIST of 2");
    Jlp7Var *lr = jlp7_env_get(env, "lr");
    ASSERT(lr && lr->type == JLP7_ARRAY && lr->val.arr[0] == 0.2, "double[] modified in Java");
    Jlp7Var *v = jlp7_env_get(env, "optWasNull");
    ASSERT(v && v->type == JLP7_BOOL && v->val.b == 1, "Java saw None as null");
    v = jlp7_env_get(env, "twice");
    ASSERT(v && v->val.i == 10, "small int arrives as int: 'int twice = small * 2' compiles");
    v = jlp7_env_get(env, "bigger");
    ASSERT(v && v->val.i == 3000000001LL, "large int arrives as long");

    jlp7_env_free(env);
}

static void test_java_export_types(void) {
    printf("\n── Java: List / Map / array / NaN declared in Java ──\n");
    if (!has_jshell()) { printf("  ⚠ jshell not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("java");
    Jlp7Env   *env = jlp7_env_new();

    int rc = jlp7_exec(
        "Map<String, Object> m = new LinkedHashMap<>();\n"
        "m.put(\"k\", 1);\n"
        "m.put(\"s\", \"x\\\"y\");\n"
        "m.put(\"nums\", List.of(1, 2, 3));\n"
        "List<String> names = new ArrayList<>(List.of(\"a\", \"b\"));\n"
        "int[] nums = {4, 5, 6};\n"
        "double d = Double.NaN;\n"
        "String q = \"line1\\nline2 \\\"quoted\\\" \\\\ \\u00e9\";\n"
        "/p\n"
        "count = len(names) + len(m)\n"
        "p/\n",
        &cfg, env);
    ASSERT(rc == 0, "exec returned 0");

    Jlp7Var *m = jlp7_env_get(env, "m");
    ASSERT(m && m->type == JLP7_DICT && m->arr_len == 3, "Map -> DICT");
    ASSERT(jlp7_dict_get(m, "k")->val.i == 1, "m.k == 1");
    ASSERT(strcmp(jlp7_dict_get(m, "s")->val.s, "x\"y") == 0, "m.s has its quote");
    ASSERT(jlp7_dict_get(m, "nums")->type == JLP7_ARRAY &&
           jlp7_dict_get(m, "nums")->arr_len == 3, "List<Integer> inside a Map -> ARRAY");
    Jlp7Var *names = jlp7_env_get(env, "names");
    ASSERT(names && names->type == JLP7_LIST && names->arr_len == 2 &&
           strcmp(jlp7_list_at(names, 1)->val.s, "b") == 0, "List<String> -> LIST");
    Jlp7Var *nums = jlp7_env_get(env, "nums");
    ASSERT(nums && nums->type == JLP7_ARRAY && nums->arr_len == 3 && nums->val.arr[2] == 6,
           "int[] -> ARRAY");
    Jlp7Var *d = jlp7_env_get(env, "d");
    ASSERT(d && d->type == JLP7_FLOAT && isnan(d->val.f), "NaN survives");
    Jlp7Var *q = jlp7_env_get(env, "q");
    ASSERT(q && strcmp(q->val.s, "line1\nline2 \"quoted\" \\ \xc3\xa9") == 0,
           "quotes, newline, backslash, e-acute in a String");
    Jlp7Var *cnt = jlp7_env_get(env, "count");
    ASSERT(cnt && cnt->val.i == 5, "python saw the Java collections (2 + 3)");

    jlp7_env_free(env);
}

/* ── C integration (requires gcc) ───────────────────────────────────── */

static int has_gcc(void) {
    FILE *f = popen("which gcc 2>/dev/null", "r");
    if (!f) return 0;
    char buf[8];
    int found = fgets(buf, sizeof(buf), f) && buf[0] != '\0';
    pclose(f);
    return found;
}

static void test_c_basic(void) {
    printf("\n── C: basic variable round-trip ──\n");
    if (!has_gcc()) { printf("  ⚠ gcc not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("c");
    Jlp7Env   *env = jlp7_env_new();

    int rc = jlp7_exec(
        "long long x = 5;\n"
        "long long y = 10;\n"
        "/p\n"
        "x = x + 1\n"
        "result = x * y\n"
        "p/\n"
        "printf(\"result = %lld\\n\", result);\n",
        &cfg, env);

    ASSERT(rc == 0, "exec returned 0");
    Jlp7Var *v;
    v = jlp7_env_get(env, "x");
    ASSERT(v && v->val.i == 6,  "x = 6 after python block");
    v = jlp7_env_get(env, "result");
    ASSERT(v && v->val.i == 60, "result = 60");

    jlp7_env_free(env);
}

static void test_c_strings(void) {
    printf("\n── C: string round-trip ──\n");
    if (!has_gcc()) { printf("  ⚠ gcc not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("c");
    Jlp7Env   *env = jlp7_env_new();

    /* String is injected as a global char[] from env */
    jlp7_env_set_str(env, "greeting", "hello");

    int rc = jlp7_exec(
        "/p\n"
        "greeting = greeting.upper() + ' WORLD'\n"
        "p/\n"
        "printf(\"%s\\n\", greeting);\n",
        &cfg, env);

    ASSERT(rc == 0, "exec returned 0");
    Jlp7Var *v = jlp7_env_get(env, "greeting");
    ASSERT(v && strcmp(v->val.s, "HELLO WORLD") == 0, "string mutated correctly");

    jlp7_env_free(env);
}

static void test_c_multi_block(void) {
    printf("\n── C: multi-block pipeline ──\n");
    if (!has_gcc()) { printf("  ⚠ gcc not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("c");
    Jlp7Env   *env = jlp7_env_new();

    int rc = jlp7_exec(
        "long long counter = 0;\n"
        "/p\n"
        "counter = counter + 10\n"
        "p/\n"
        "long long doubled = counter * 2;\n"
        "/p\n"
        "final_val = doubled + 1\n"
        "p/\n",
        &cfg, env);

    ASSERT(rc == 0, "exec returned 0");
    Jlp7Var *v;
    v = jlp7_env_get(env, "counter");
    ASSERT(v && v->val.i == 10,  "counter = 10");
    v = jlp7_env_get(env, "doubled");
    ASSERT(v && v->val.i == 20,  "doubled = 20");
    v = jlp7_env_get(env, "final_val");
    ASSERT(v && v->val.i == 21,  "final_val = 21");

    jlp7_env_free(env);
}

static void test_c_compile_error(void) {
    printf("\n── C: compile error handling ──\n");
    if (!has_gcc()) { printf("  ⚠ gcc not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("c");
    Jlp7Env   *env = jlp7_env_new();

    int rc = jlp7_exec("this is not valid C code!!!\n", &cfg, env);
    ASSERT(rc != 0, "compile error returns -1");

    jlp7_env_free(env);
}

/* ── C: exported structs ─────────────────────────────────────────────── */

static void test_c_struct_export(void) {
    printf("\n── C: // jlp7:export structs, C -> env ──\n");
    if (!has_gcc()) { printf("  ⚠ gcc not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("c");
    Jlp7Env   *env = jlp7_env_new();
    Jlp7Error  err;

    int rc = jlp7_exec_ex(
        "// jlp7:export\n"
        "struct Point { double x, y; };\n"
        "// jlp7:export\n"
        "typedef struct {\n"
        "    char   name[16];   // a string\n"
        "    int    id;\n"
        "    bool   alive;\n"
        "    struct Point pos;\n"
        "    double hist[3];\n"
        "    struct Point trail[2];\n"
        "} Entity;\n"
        "\n"
        "struct Point p = {1.5, 2.5};\n"
        "Entity e = { \"b\\\"o\\\"b\", 7, true, {3, 4}, {1, 2, 3}, {{5, 6}, {7, 8}} };\n"
        "Entity es[2] = { {\"a\", 1, false, {0, 0}, {0, 0, 0}, {{0, 0}, {0, 0}}},\n"
        "                 {\"b\", 2, true,  {1, 1}, {9, 9, 9}, {{0, 0}, {0, 0}}} };\n"
        "char label[] = \"say \\\"hi\\\"\\n\";\n"
        "double nan_val = 0.0 / 0.0;\n"
        "/p\n"
        "total = p['x'] + p['y'] + e['pos']['x'] + e['hist'][2]\n"
        "ename = e['name']\n"
        "second = es[1]['name']\n"
        "p/\n",
        &cfg, env, &err);
    ASSERT(rc == 0, "exec with exported structs succeeded");
    if (rc != 0) { fprintf(stderr, "%s\n", err.message ? err.message : "?"); jlp7_error_clear(&err); }

    Jlp7Var *p = jlp7_env_get(env, "p");
    ASSERT(p && p->type == JLP7_DICT && p->arr_len == 2, "struct -> DICT");
    ASSERT(p && jlp7_dict_get(p, "x")->val.f == 1.5, "p.x == 1.5");

    Jlp7Var *e = jlp7_env_get(env, "e");
    ASSERT(e && e->type == JLP7_DICT && e->arr_len == 6, "Entity has 6 fields");
    ASSERT(strcmp(jlp7_dict_get(e, "name")->val.s, "b\"o\"b") == 0,
           "char[16] -> string, quotes escaped");
    ASSERT(jlp7_dict_get(e, "id")->val.i == 7, "int field");
    ASSERT(jlp7_dict_get(e, "alive")->type == JLP7_BOOL && jlp7_dict_get(e, "alive")->val.b == 1,
           "bool field");
    ASSERT(jlp7_dict_get(jlp7_dict_get(e, "pos"), "y")->val.f == 4, "nested struct");
    Jlp7Var *hist = jlp7_dict_get(e, "hist");
    ASSERT(hist && hist->type == JLP7_ARRAY && hist->arr_len == 3 && hist->val.arr[2] == 3,
           "double[3] field -> ARRAY");
    Jlp7Var *trail = jlp7_dict_get(e, "trail");
    ASSERT(trail && trail->type == JLP7_LIST && trail->arr_len == 2 &&
           jlp7_dict_get(jlp7_list_at(trail, 1), "y")->val.f == 8,
           "struct[2] field -> LIST of DICT");

    Jlp7Var *es = jlp7_env_get(env, "es");
    ASSERT(es && es->type == JLP7_LIST && es->arr_len == 2, "Entity es[2] -> LIST of 2");
    ASSERT(strcmp(jlp7_dict_get(jlp7_list_at(es, 1), "name")->val.s, "b") == 0,
           "es[1].name == b");

    Jlp7Var *lab = jlp7_env_get(env, "label");
    ASSERT(lab && lab->type == JLP7_STRING && strcmp(lab->val.s, "say \"hi\"\n") == 0,
           "user-declared char[] is a string (quotes and newline escaped)");
    Jlp7Var *nv = jlp7_env_get(env, "nan_val");
    ASSERT(nv && nv->type == JLP7_FLOAT && isnan(nv->val.f), "NaN from C");

    Jlp7Var *tot = jlp7_env_get(env, "total");
    ASSERT(tot && tot->val.f == 1.5 + 2.5 + 3 + 3, "Python read the C structs");
    Jlp7Var *en = jlp7_env_get(env, "ename");
    ASSERT(en && strcmp(en->val.s, "b\"o\"b") == 0, "Python read e['name']");

    jlp7_env_free(env);
}

static void test_c_struct_import(void) {
    printf("\n── C: // jlp7:export structs, env -> C ──\n");
    if (!has_gcc()) { printf("  ⚠ gcc not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("c");
    Jlp7Env   *env = jlp7_env_new();
    Jlp7Error  err;

    /* Python makes dicts; the C block declares structs with no initialiser
     * and finds them filled in. */
    int rc = jlp7_exec_ex(
        "/p\n"
        "e = {'name': 'bob', 'id': 7, 'alive': True, 'pos': {'x': 1, 'y': 2},\n"
        "     'hist': [1, 2, 3], 'trail': [{'x': 0, 'y': 1}, {'x': 2, 'y': 3}],\n"
        "     'extra': 'ignored'}\n"
        "es = [{'id': 10}, {'id': 20, 'name': 'zed'}]\n"
        "p/\n"
        "// jlp7:export\n"
        "struct Point { double x, y; };\n"
        "// jlp7:export\n"
        "typedef struct {\n"
        "    char name[16]; int id; bool alive;\n"
        "    struct Point pos; double hist[3]; struct Point trail[2];\n"
        "} Entity;\n"
        "Entity e;\n"
        "Entity es[2];\n"
        "Entity fresh;\n"
        "int seen = e.id + es[0].id + es[1].id;\n"
        "e.id += 1;\n"
        "e.pos.x = 9;\n"
        "e.hist[2] = 30;\n"
        "e.trail[1].y = 99;\n"
        "strcpy(e.name, \"alice\");\n"
        "es[1].alive = true;\n"
        "fresh.id = -5;\n",
        &cfg, env, &err);
    ASSERT(rc == 0, "exec succeeded");
    if (rc != 0) { fprintf(stderr, "%s\n", err.message ? err.message : "?"); jlp7_error_clear(&err); }

    Jlp7Var *seen = jlp7_env_get(env, "seen");
    ASSERT(seen && seen->val.i == 7 + 10 + 20, "C saw the fields Python set");

    Jlp7Var *e = jlp7_env_get(env, "e");
    ASSERT(e && jlp7_dict_get(e, "id")->val.i == 8, "C changed e.id");
    ASSERT(strcmp(jlp7_dict_get(e, "name")->val.s, "alice") == 0, "C changed e.name");
    ASSERT(jlp7_dict_get(jlp7_dict_get(e, "pos"), "x")->val.f == 9, "C changed e.pos.x");
    ASSERT(jlp7_dict_get(jlp7_dict_get(e, "pos"), "y")->val.f == 2, "e.pos.y kept");
    ASSERT(jlp7_dict_get(e, "hist")->val.arr[2] == 30 &&
           jlp7_dict_get(e, "hist")->val.arr[0] == 1, "C changed e.hist[2], kept hist[0]");
    ASSERT(jlp7_dict_get(jlp7_list_at(jlp7_dict_get(e, "trail"), 1), "y")->val.f == 99 &&
           jlp7_dict_get(jlp7_list_at(jlp7_dict_get(e, "trail"), 0), "y")->val.f == 1,
           "C changed e.trail[1].y, kept trail[0].y");
    ASSERT(jlp7_dict_get(e, "extra") == NULL,
           "keys that are not fields are not carried (the C struct is the truth)");

    Jlp7Var *es = jlp7_env_get(env, "es");
    ASSERT(es && jlp7_dict_get(jlp7_list_at(es, 1), "alive")->val.b == 1 &&
           strcmp(jlp7_dict_get(jlp7_list_at(es, 1), "name")->val.s, "zed") == 0,
           "struct array filled from a LIST, changed in C");

    Jlp7Var *fresh = jlp7_env_get(env, "fresh");
    ASSERT(fresh && jlp7_dict_get(fresh, "id")->val.i == -5,
           "no env dict: declared struct is just exported");

    /* An initialiser wins over the env, like a redeclared int does. */
    jlp7_env_free(env);
    env = jlp7_env_new();
    rc = jlp7_exec(
        "/p\nq = {'x': 100, 'y': 200}\np/\n"
        "// jlp7:export\nstruct Point { double x, y; };\n"
        "struct Point q = {1, 2};\n",
        &cfg, env);
    Jlp7Var *q = jlp7_env_get(env, "q");
    ASSERT(rc == 0 && q && jlp7_dict_get(q, "x")->val.f == 1, "initialiser beats the env dict");

    jlp7_env_free(env);
}

static void test_c_struct_passthrough_and_errors(void) {
    printf("\n── C: DICT/LIST pass-through and struct errors ──\n");
    if (!has_gcc()) { printf("  ⚠ gcc not found — skipping\n"); return; }

    Jlp7Config cfg = jlp7_default_config("c");
    Jlp7Env   *env = jlp7_env_new();

    /* A C block with no struct: dict and list in the env come out unchanged. */
    int rc = jlp7_exec(
        "/p\ncfg = {'a': [1, 'two'], 'b': None}\nnames = ['x', 'y']\np/\n"
        "long long n = 5;\n",
        &cfg, env);
    ASSERT(rc == 0, "block without structs ran");
    Jlp7Var *cfgv = jlp7_env_get(env, "cfg");
    ASSERT(cfgv && cfgv->type == JLP7_DICT && jlp7_dict_get(cfgv, "b")->type == JLP7_NULL,
           "DICT passed through the C block untouched");
    Jlp7Var *names = jlp7_env_get(env, "names");
    ASSERT(names && names->type == JLP7_LIST && names->arr_len == 2,
           "LIST passed through the C block untouched");

    /* Unsupported field: clear error, env rolled back */
    size_t before = env->count;
    rc = jlp7_exec("// jlp7:export\nstruct Bad { char *name; };\nlong long z = 1;\n", &cfg, env);
    ASSERT(rc == -1 && env->count == before, "pointer field: error, env unchanged");

    rc = jlp7_exec("// jlp7:export\nint not_a_struct = 1;\n", &cfg, env);
    ASSERT(rc == -1, "marker not followed by a struct: error");

    /* Plain structs without the marker behave as before (not exported) */
    rc = jlp7_exec("struct Q { int a; };\nstruct Q q = {3};\nlong long w = q.a;\n", &cfg, env);
    Jlp7Var *w = jlp7_env_get(env, "w");
    ASSERT(rc == 0 && w && w->val.i == 3 && jlp7_env_get(env, "q") == NULL,
           "unmarked struct is not exported");

    jlp7_env_free(env);
}

/* ── main ───────────────────────────────────────────────────────────── */

int main(void) {
    printf("JLP7 — Jean-Luc's Practical Purposeful Pre-Processed Polyglot Python Project\n");
    printf("Test Suite\n");
    printf("══════════════════════════════════════════════════════════════════════════════\n");

    test_parser_basic();
    test_parser_multiple_py();
    test_parser_no_python();
    test_env();
    test_python_arithmetic();
    test_python_strings();
    test_python_bool();
    test_python_error();
    test_error_struct();
    test_transactional_env();
    test_marshal_errors();
    test_json_nested();
    test_value_api();
    test_python_nested();
    test_python_dedent();
    test_allowpy_false();
    test_java_basic();
    test_java_strings();
    test_java_multi_block();
    test_java_nested();
    test_java_export_types();
    test_c_basic();
    test_c_strings();
    test_c_multi_block();
    test_c_compile_error();
    test_c_struct_export();
    test_c_struct_import();
    test_c_struct_passthrough_and_errors();

    printf("\n══════════════════════════════════════════════════════════════════════════════\n");
    printf("  Passed: %d  |  Failed: %d\n", passed, failed);
    printf("══════════════════════════════════════════════════════════════════════════════\n");
    return failed > 0 ? 1 : 0;
}
