// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "jlp7.h"
#include "java_internal.h"

/*
 * runner_java.c — JShell subprocess driver.
 *
 * Delegates source assembly to java_builder.c and
 * JSON parsing to java_json.c.
 * This file only handles: temp file, subprocess, stdout capture.
 */

#define MAX_LINE 4096

int jlp7_run_java(const char *code, Jlp7Env *env) {
    /* 1. Scan declared variables in this block */
    int          ndecls = 0;
    Jlp7VarDecl *decls  = jlp7_java_scan_decls(code, &ndecls);

    /* 2. Build full JShell source (dynamically allocated, no truncation) */
    char *full_src = jlp7_java_build_source(code, env, decls, ndecls);
    free(decls);

    /* 3. Write to a secure temp file via mkstemp */
    char tmppath[] = "/tmp/jlp7_XXXXXX";
    int  tmpfd     = mkstemp(tmppath);
    if (tmpfd < 0) {
        fprintf(stderr, "[jlp7:java] failed to create temp file\n");
        free(full_src);
        return -1;
    }

    /* Rename with .jsh extension so JShell accepts it */
    char jshpath[64];
    snprintf(jshpath, sizeof(jshpath), "%s.jsh", tmppath);
    rename(tmppath, jshpath);

    FILE *f = fdopen(tmpfd, "w");
    if (!f) {
        fprintf(stderr, "[jlp7:java] failed to open temp file for writing\n");
        free(full_src);
        remove(jshpath);
        return -1;
    }
    fputs(full_src, f);
    fclose(f);
    free(full_src);

    /* 4. Build error capture path */
    char errpath[80];
    snprintf(errpath, sizeof(errpath), "/tmp/jlp7_err_%d.txt", (int)getpid());

    /* 5. Run JShell */
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "jshell --execution local --feedback silent %s 2>%s",
             jshpath, errpath);

    FILE *pipe = popen(cmd, "r");
    if (!pipe) {
        fprintf(stderr, "[jlp7:java] failed to launch jshell\n");
        remove(jshpath);
        return -1;
    }

    /* 6. Read stdout: pass-through user output, capture __VARS__ line.
     * getline() grows its buffer as needed, unlike a fixed fgets buffer
     * (see the identical fix in runner_c.c for why this matters). */
    char   *line      = NULL;
    size_t  line_cap  = 0;
    char   *vars_json = NULL;

    while (getline(&line, &line_cap, pipe) != -1) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';

        if (strncmp(line, "__VARS__:", 9) == 0) {
            free(vars_json);
            vars_json = strdup(line + 9);
        } else if (len > 0) {
            puts(line);
        }
    }

    int exit_status = pclose(pipe);

    /* 7. Surface any JShell errors */
    FILE *ef = fopen(errpath, "r");
    if (ef) {
        int printed_header = 0;
        while (getline(&line, &line_cap, ef) != -1) {
            if (!printed_header) {
                fprintf(stderr, "[jlp7:java] errors:\n");
                printed_header = 1;
            }
            fputs(line, stderr);
        }
        fclose(ef);
    }
    free(line);

    /* 8. Clean up temp files */
    remove(jshpath);
    remove(errpath);

    if (exit_status != 0) {
        fprintf(stderr, "[jlp7:java] jshell exited with status %d\n", exit_status);
        free(vars_json);
        return -1;
    }

    /* 9. Parse vars back into env */
    if (vars_json && vars_json[0])
        jlp7_java_parse_vars(vars_json, env);
    free(vars_json);

    return 0;
}
