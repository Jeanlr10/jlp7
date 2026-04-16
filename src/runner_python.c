// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "jlp7.h"

/*
 * runner_python.c — embeds libpython directly (no subprocess).
 *
 * Variables flow in via a PyDict built from Jlp7Env.
 * After execution, mutations are read back from that dict into Jlp7Env.
 * Errors are captured as a string and printed to stderr.
 */

static int python_initialized = 0;

static void ensure_python(void) {
    if (!python_initialized) {
        Py_Initialize();
        python_initialized = 1;
    }
}

/* Capture the current Python exception as a human-readable string.
 * Caller must free the returned string. Returns NULL if no exception. */
static char *capture_python_error(void) {
    if (!PyErr_Occurred()) return NULL;

    PyObject *type, *value, *tb;
    PyErr_Fetch(&type, &value, &tb);
    PyErr_NormalizeException(&type, &value, &tb);

    char *msg = NULL;

    /* Try traceback module for full formatted error */
    PyObject *tb_mod = PyImport_ImportModule("traceback");
    if (tb_mod) {
        PyObject *fmt_fn = PyObject_GetAttrString(tb_mod, "format_exception");
        if (fmt_fn) {
            PyObject *lines = PyObject_CallFunctionObjArgs(
                fmt_fn,
                type  ? type  : Py_None,
                value ? value : Py_None,
                tb    ? tb    : Py_None,
                NULL);
            if (lines && PyList_Check(lines)) {
                PyObject *joined = PyUnicode_Join(PyUnicode_FromString(""), lines);
                if (joined) {
                    const char *s = PyUnicode_AsUTF8(joined);
                    if (s) msg = strdup(s);
                    Py_DECREF(joined);
                }
                Py_DECREF(lines);
            }
            Py_DECREF(fmt_fn);
        }
        Py_DECREF(tb_mod);
    }

    /* Fallback: just stringify the value */
    if (!msg && value) {
        PyObject *str = PyObject_Str(value);
        if (str) {
            const char *s = PyUnicode_AsUTF8(str);
            if (s) msg = strdup(s);
            Py_DECREF(str);
        }
    }

    Py_XDECREF(type);
    Py_XDECREF(value);
    Py_XDECREF(tb);

    return msg ? msg : strdup("(unknown Python error)");
}

/* Jlp7Env → PyDict */
static PyObject *env_to_pydict(const Jlp7Env *env) {
    PyObject *d = PyDict_New();
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];
        PyObject *val = NULL;
        switch (v->type) {
            case JLP7_INT:    val = PyLong_FromLongLong(v->val.i);  break;
            case JLP7_FLOAT:  val = PyFloat_FromDouble(v->val.f);   break;
            case JLP7_BOOL:   val = PyBool_FromLong(v->val.b);      break;
            case JLP7_STRING: val = PyUnicode_FromString(v->val.s); break;
        }
        if (val) {
            PyDict_SetItemString(d, v->name, val);
            Py_DECREF(val);
        }
    }
    return d;
}

/* PyDict → Jlp7Env: read back mutations and new variables */
static void pydict_to_env(PyObject *d, Jlp7Env *env) {
    PyObject *key, *val;
    Py_ssize_t pos = 0;

    while (PyDict_Next(d, &pos, &key, &val)) {
        const char *name = PyUnicode_AsUTF8(key);
        if (!name)                 continue;
        if (name[0] == '_')        continue;   /* skip dunder / private */
        if (PyCallable_Check(val)) continue;
        if (PyType_Check(val))     continue;
        if (PyModule_Check(val))   continue;

        if (PyBool_Check(val)) {
            /* Must check bool before long — bool is a subtype of int */
            jlp7_env_set_bool(env, name, PyObject_IsTrue(val));
        } else if (PyLong_Check(val)) {
            jlp7_env_set_int(env, name, PyLong_AsLongLong(val));
        } else if (PyFloat_Check(val)) {
            jlp7_env_set_float(env, name, PyFloat_AsDouble(val));
        } else if (PyUnicode_Check(val)) {
            jlp7_env_set_str(env, name, PyUnicode_AsUTF8(val));
        }
        /* Lists, dicts, etc. are currently ignored */
    }
}

int jlp7_run_python(const char *code, Jlp7Env *env) {
    ensure_python();

    /* Fresh globals with builtins */
    PyObject *builtins = PyImport_ImportModule("builtins");
    PyObject *globals  = PyDict_New();
    PyDict_SetItemString(globals, "__builtins__", builtins);
    Py_DECREF(builtins);

    /* Inject env as locals */
    PyObject *locals = env_to_pydict(env);

    /* Compile first to get better error locations */
    PyObject *code_obj = Py_CompileString(code, "<jlp7:python>", Py_file_input);
    if (!code_obj) {
        char *err = capture_python_error();
        fprintf(stderr, "[jlp7:python] compile error:\n%s\n", err);
        free(err);
        Py_DECREF(globals);
        Py_DECREF(locals);
        return -1;
    }

    PyObject *result = PyEval_EvalCode(code_obj, globals, locals);
    Py_DECREF(code_obj);

    if (!result) {
        char *err = capture_python_error();
        fprintf(stderr, "[jlp7:python] runtime error:\n%s\n", err);
        free(err);
        Py_DECREF(globals);
        Py_DECREF(locals);
        return -1;
    }
    Py_DECREF(result);

    pydict_to_env(locals, env);

    Py_DECREF(globals);
    Py_DECREF(locals);
    return 0;
}
