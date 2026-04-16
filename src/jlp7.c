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
    return cfg;
}

int jlp7_exec(const char *source, Jlp7Config *cfg, Jlp7Env *env) {
    Jlp7Block *blocks = jlp7_parse(source);
    Jlp7Block *b      = blocks;
    int        rc     = 0;

    while (b && rc == 0) {
        if (cfg->debug) {
            fprintf(stderr, "[jlp7] running %s block\n",
                    b->type == JLP7_BLOCK_PYTHON ? "python" : cfg->language);
        }

        if (b->type == JLP7_BLOCK_PYTHON) {
            if (!cfg->allowpy) {
                fprintf(stderr,
                        "[jlp7] error: /p...p/ block found but allowpy=0\n");
                rc = -1;
                break;
            }
            rc = jlp7_run_python(b->code, env);

        } else {
            if (strcmp(cfg->language, "java") == 0) {
                rc = jlp7_run_java(b->code, env);
            } else if (strcmp(cfg->language, "c") == 0) {
                rc = jlp7_run_c(b->code, env);
            } else {
                fprintf(stderr, "[jlp7] unsupported language: '%s'\n",
                        cfg->language);
                rc = -1;
            }
        }

        if (cfg->debug) jlp7_env_dump(env);
        b = b->next;
    }

    jlp7_blocks_free(blocks);
    return rc;
}
