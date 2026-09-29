// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jlp7.h"

Jlp7Config jlp7_default_config(const char *language) {
    Jlp7Config cfg;
    cfg.language = language;
    cfg.allowpy  = 1;
    cfg.debug    = 0;
    cfg.strict   = 0;
    return cfg;
}

static void set_simple_error(Jlp7Error *err, Jlp7ErrorKind kind,
                             const char *msg, int index, int line) {
    if (!err) return;
    jlp7_error_clear(err);
    err->kind        = kind;
    err->message     = strdup(msg);
    err->block_index = index;
    err->line        = line;
}

int jlp7_exec_ex(const char *source, Jlp7Config *cfg, Jlp7Env *env,
                 Jlp7Error *err) {
    if (err) { memset(err, 0, sizeof(*err)); err->block_index = -1; }

    /* Snapshot: on failure, env goes back to exactly this. */
    Jlp7Env *snapshot = jlp7_env_clone(env);
    if (!snapshot) {
        set_simple_error(err, JLP7_ERR_INTERNAL, "out of memory", -1, 0);
        return -1;
    }

    Jlp7Block *blocks = jlp7_parse(source);
    Jlp7Block *b      = blocks;
    int        rc     = 0;
    int        index  = 0;

    while (b && rc == 0) {
        if (cfg->debug) {
            fprintf(stderr, "[jlp7] running %s block\n",
                    b->type == JLP7_BLOCK_PYTHON ? "python" : cfg->language);
        }

        if (b->type == JLP7_BLOCK_PYTHON) {
            if (!cfg->allowpy) {
                if (!err) fprintf(stderr,
                        "[jlp7] error: /p...p/ block found but allowpy=0\n");
                set_simple_error(err, JLP7_ERR_CONFIG,
                        "/p...p/ block found but allowpy=0", index, b->line);
                rc = -1;
                break;
            }
            rc = jlp7_run_python_ex(b->code, env, b->line, cfg->strict, err);
            if (rc != 0 && err) err->block_index = index;

        } else {
            if (strcmp(cfg->language, "java") == 0) {
                rc = jlp7_run_java(b->code, env);
            } else if (strcmp(cfg->language, "c") == 0) {
                rc = jlp7_run_c(b->code, env);
            } else {
                if (!err) fprintf(stderr, "[jlp7] unsupported language: '%s'\n",
                        cfg->language);
                set_simple_error(err, JLP7_ERR_CONFIG,
                        "unsupported language", index, 0);
                rc = -1;
                break;
            }
            if (rc != 0)
                set_simple_error(err, JLP7_ERR_FOREIGN,
                        "foreign block failed (details on stderr)",
                        index, b->line);
        }

        if (cfg->debug) jlp7_env_dump(env);
        b = b->next;
        index++;
    }

    jlp7_blocks_free(blocks);

    if (rc != 0) jlp7_env_replace(env, snapshot);   /* roll back */
    else         jlp7_env_free(snapshot);
    return rc;
}

int jlp7_exec(const char *source, Jlp7Config *cfg, Jlp7Env *env) {
    return jlp7_exec_ex(source, cfg, env, NULL);
}
