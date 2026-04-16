// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include "jlp7.h"

/*
 * jlp7_parse — split a JLP7 source string into a linked list of blocks.
 *
 * Syntax:
 *   /p          (optionally followed by whitespace/newline)
 *   ...python...
 *   p/
 *
 * Everything outside /p...p/ is a FOREIGN block.
 * Everything inside  /p...p/ is a PYTHON  block.
 */

static Jlp7Block *make_block(Jlp7BlockType type, const char *start, size_t len) {
    Jlp7Block *b = malloc(sizeof(Jlp7Block));
    b->type      = type;
    b->code      = malloc(len + 1);
    memcpy(b->code, start, len);
    b->code[len] = '\0';
    b->next      = NULL;
    return b;
}

/* Append a block to a list, maintaining head/tail pointers. */
static void list_append(Jlp7Block **head, Jlp7Block **tail, Jlp7Block *b) {
    if (!*head) *head = b;
    else        (*tail)->next = b;
    *tail = b;
}

Jlp7Block *jlp7_parse(const char *source) {
    Jlp7Block *head = NULL;
    Jlp7Block *tail = NULL;

    const char *p   = source;
    const char *seg = source;   /* start of current foreign segment */

    while (*p) {
        /* Detect /p opener: must be followed by whitespace, newline, or end */
        if (p[0] == '/' && p[1] == 'p' &&
            (!p[2] || p[2] == ' ' || p[2] == '\t' || p[2] == '\n' || p[2] == '\r')) {

            /* Flush any preceding foreign content */
            if (p > seg) {
                list_append(&head, &tail,
                            make_block(JLP7_BLOCK_FOREIGN, seg, p - seg));
            }

            /* Skip "/p" and one optional newline */
            p += 2;
            if (*p == '\r') p++;
            if (*p == '\n') p++;

            /* Find closing "p/" */
            const char *py_start = p;
            while (*p) {
                if (p[0] == 'p' && p[1] == '/') break;
                p++;
            }

            list_append(&head, &tail,
                        make_block(JLP7_BLOCK_PYTHON, py_start, p - py_start));

            if (*p) p += 2;   /* consume "p/" */
            if (*p == '\r') p++;
            if (*p == '\n') p++;
            seg = p;

        } else {
            p++;
        }
    }

    /* Flush any trailing foreign content */
    if (p > seg) {
        list_append(&head, &tail,
                    make_block(JLP7_BLOCK_FOREIGN, seg, p - seg));
    }

    return head;
}

void jlp7_blocks_free(Jlp7Block *head) {
    while (head) {
        Jlp7Block *next = head->next;
        free(head->code);
        free(head);
        head = next;
    }
}
