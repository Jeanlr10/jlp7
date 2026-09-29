// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jlp7.h"

/*
 * thread_test.c — many threads, one env each, all using Python blocks.
 *
 * This is its own program (not part of main.c) so that the very first
 * Python block of the process is a race between threads: it tests the
 * one-time interpreter start-up, not just steady state.
 */

#define NTHREADS 16
#define NITERS   200

static pthread_barrier_t start_line;
static int with_c;

typedef struct {
    int       id;
    int       ok;
    char      why[160];
} Result;

static void *worker(void *arg) {
    Result *r = arg;
    r->ok = 0;
    Jlp7Config cfg = jlp7_default_config("c");
    Jlp7Env   *env = jlp7_env_new();
    jlp7_env_set_int(env, "n", 0);
    jlp7_env_set_int(env, "id", r->id);

    pthread_barrier_wait(&start_line);

    long long expect = 0;
    for (int i = 0; i < NITERS; i++) {
        Jlp7Error err;
        int rc;
        if (i % 10 == 9) {
            /* Failing block: must roll back the n = n + 1000 */
            rc = jlp7_exec_ex("/p\nn = n + 1000\nraise ValueError('x')\np/\n",
                              &cfg, env, &err);
            if (rc == 0 || err.kind != JLP7_ERR_PY_RUNTIME) {
                snprintf(r->why, sizeof(r->why), "iter %d: expected runtime error", i);
                jlp7_error_clear(&err);
                goto out;
            }
            jlp7_error_clear(&err);
        } else {
            rc = jlp7_exec("/p\nn = n + 1\np/\n", &cfg, env);
            if (rc != 0) {
                snprintf(r->why, sizeof(r->why), "iter %d: rc=%d", i, rc);
                goto out;
            }
            expect++;
        }
        Jlp7Var *v = jlp7_env_get(env, "n");
        if (!v || v->val.i != expect) {
            snprintf(r->why, sizeof(r->why), "iter %d: n=%lld expected %lld",
                     i, v ? v->val.i : -1, expect);
            goto out;
        }
    }

    if (with_c) {
        /* Foreign block in a child process, then Python again. */
        int rc = jlp7_exec("long long k = n * 2;\n/p\nk = k + id\np/\n",
                           &cfg, env);
        Jlp7Var *v = jlp7_env_get(env, "k");
        if (rc != 0 || !v || v->val.i != expect * 2 + r->id) {
            snprintf(r->why, sizeof(r->why), "c block: rc=%d k=%lld",
                     rc, v ? v->val.i : -1);
            goto out;
        }
    }
    r->ok = 1;
out:
    jlp7_env_free(env);
    return NULL;
}

int main(int argc, char **argv) {
    with_c = (argc > 1 && strcmp(argv[1], "--with-c") == 0);
    pthread_t th[NTHREADS];
    Result    res[NTHREADS];

    pthread_barrier_init(&start_line, NULL, NTHREADS);
    for (int i = 0; i < NTHREADS; i++) {
        memset(&res[i], 0, sizeof(res[i]));
        res[i].id = i;
        pthread_create(&th[i], NULL, worker, &res[i]);
    }
    int bad = 0;
    for (int i = 0; i < NTHREADS; i++) {
        pthread_join(th[i], NULL);
        if (!res[i].ok) {
            printf("  ✗ thread %d: %s\n", i, res[i].why);
            bad++;
        }
    }
    printf("%s: %d threads x %d blocks%s, %d failed\n",
           bad ? "FAIL" : "PASS", NTHREADS, NITERS,
           with_c ? " (+ C block)" : "", bad);
    return bad ? 1 : 0;
}
