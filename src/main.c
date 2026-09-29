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
    test_allowpy_false();
    test_java_basic();
    test_java_strings();
    test_java_multi_block();
    test_c_basic();
    test_c_strings();
    test_c_multi_block();
    test_c_compile_error();

    printf("\n══════════════════════════════════════════════════════════════════════════════\n");
    printf("  Passed: %d  |  Failed: %d\n", passed, failed);
    printf("══════════════════════════════════════════════════════════════════════════════\n");
    return failed > 0 ? 1 : 0;
}
