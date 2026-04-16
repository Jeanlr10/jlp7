// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jlp7.h"

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
