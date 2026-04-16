// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "jlp7.h"
#include "c_internal.h"

/*
 * runner_c.c — compiles and runs a C block via gcc.
 *
 * Flow:
 *   1. Scan block for variable declarations
 *   2. Build a self-contained C program (preamble + user code + JSON printer)
 *   3. Write to /tmp/jlp7_c_XXXXXX.c via mkstemp
 *   4. Compile: gcc -o /tmp/jlp7_c_XXXXXX /tmp/jlp7_c_XXXXXX.c
 *   5. Run the binary, capture stdout
 *   6. Parse __VARS__:{...} back into env
 *   7. Clean up temp files
 */

#define MAX_LINE 4096

int jlp7_run_c(const char *code, Jlp7Env *env) {
    /* 1. Scan declarations */
    int           ndecls = 0;
    Jlp7CVarDecl *decls  = jlp7_c_scan_decls(code, &ndecls);

    /* 2. Build full C source */
    char *full_src = jlp7_c_build_source(code, env, decls, ndecls);
    free(decls);

    /* 3. Write source to secure temp file */
    char src_template[] = "/tmp/jlp7_c_XXXXXX";
    int  srcfd           = mkstemp(src_template);
    if (srcfd < 0) {
        fprintf(stderr, "[jlp7:c] failed to create temp source file\n");
        free(full_src);
        return -1;
    }

    /* Rename with .c extension so gcc is happy */
    char srcpath[80], binpath[80], errpath[80];
    snprintf(srcpath, sizeof(srcpath), "%s.c",   src_template);
    snprintf(binpath, sizeof(binpath), "%s_bin",  src_template);
    snprintf(errpath, sizeof(errpath), "%s_err",  src_template);
    rename(src_template, srcpath);

    FILE *f = fdopen(srcfd, "w");
    if (!f) {
        fprintf(stderr, "[jlp7:c] failed to open temp source for writing\n");
        free(full_src);
        remove(srcpath);
        return -1;
    }
    fputs(full_src, f);
    fclose(f);
    free(full_src);

    /* 4. Compile */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "gcc -Wall -o %s %s 2>%s",
             binpath, srcpath, errpath);

    int compile_rc = system(cmd);

    /* Surface any compiler warnings/errors */
    FILE *ef = fopen(errpath, "r");
    if (ef) {
        char line[MAX_LINE];
        int printed = 0;
        while (fgets(line, sizeof(line), ef)) {
            if (!printed) {
                fprintf(stderr, "[jlp7:c] compiler output:\n");
                printed = 1;
            }
            fputs(line, stderr);
        }
        fclose(ef);
        remove(errpath);
    }

    if (compile_rc != 0) {
        fprintf(stderr, "[jlp7:c] compilation failed\n");
        remove(srcpath);
        return -1;
    }
    remove(srcpath);

    /* 5. Run the binary */
    char run_cmd[256];
    snprintf(run_cmd, sizeof(run_cmd), "%s 2>%s", binpath, errpath);

    FILE *pipe = popen(run_cmd, "r");
    if (!pipe) {
        fprintf(stderr, "[jlp7:c] failed to run compiled binary\n");
        remove(binpath);
        return -1;
    }

    /* 6. Read stdout: pass through user output, capture __VARS__ */
    char line[MAX_LINE];
    char vars_json[MAX_LINE] = {0};

    while (fgets(line, sizeof(line), pipe)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';

        if (strncmp(line, "__VARS__:", 9) == 0) {
            strncpy(vars_json, line + 9, MAX_LINE - 1);
        } else if (len > 0) {
            puts(line);
        }
    }

    int run_rc = pclose(pipe);

    /* Surface any runtime stderr */
    ef = fopen(errpath, "r");
    if (ef) {
        char ln[MAX_LINE];
        int printed = 0;
        while (fgets(ln, sizeof(ln), ef)) {
            if (!printed) {
                fprintf(stderr, "[jlp7:c] runtime stderr:\n");
                printed = 1;
            }
            fputs(ln, stderr);
        }
        fclose(ef);
        remove(errpath);
    }

    /* 7. Clean up */
    remove(binpath);

    if (run_rc != 0) {
        fprintf(stderr, "[jlp7:c] binary exited with status %d\n", run_rc);
        return -1;
    }

    /* 8. Parse vars back into env */
    if (vars_json[0])
        jlp7_c_parse_vars(vars_json, env);

    return 0;
}
