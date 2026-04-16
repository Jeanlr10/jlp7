// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#include "jlp7.h"
#include "java_internal.h"
#include "c_internal.h"

/*
 * c_json.c — the C runner uses the same __VARS__:{...} format as the Java
 * runner, so we just delegate to the already-hardened Java JSON parser.
 */
void jlp7_c_parse_vars(const char *json, Jlp7Env *env) {
    jlp7_java_parse_vars(json, env);
}
