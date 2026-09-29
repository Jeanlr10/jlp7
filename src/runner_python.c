// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Jean-Luc Robitaille
#define _POSIX_C_SOURCE 200809L
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "jlp7.h"

/*
 * runner_python.c — embeds libpython directly (no subprocess).
 *
 * Variables flow in via a PyDict built from Jlp7Env.
 * After a successful run, the dict is exported to a scratch Jlp7Env and
 * merged into the caller's env in one step. On any failure the caller's
 * env is not touched.
 *
 * Errors are reported through Jlp7Error (see jlp7.h). When the caller
 * passes err == NULL, the message is printed to stderr instead.
 *
 * Threading: see "Threading" in jlp7.h for the contract. In short, any
 * number of threads may call in at once, each with its own Jlp7Env.
 * Every entry point takes the GIL with PyGILState_Ensure(), so it is also
 * safe to call from a thread that does not hold it (for example from
 * Python via ctypes). If Python is already running in the process it is
 * not initialised a second time.
 */

static pthread_once_t python_once = PTHREAD_ONCE_INIT;

/* Runs exactly once, on whichever thread gets here first. */
static void init_python_once(void) {
    if (Py_IsInitialized()) return;   /* hosted in a Python process */

    Py_Initialize();
    /* Py_Initialize leaves the calling thread holding the GIL. Release it,
     * or every other thread would block forever in PyGILState_Ensure().
     * The saved thread state is deliberately never restored: from here on
     * all access goes through PyGILState_Ensure/Release, including this
     * thread's. */
    (void)PyEval_SaveThread();
}

static void ensure_python(void) {
    pthread_once(&python_once, init_python_once);
}

/* ── Errors ─────────────────────────────────────────────────────────── */

void jlp7_error_clear(Jlp7Error *err) {
    if (!err) return;
    free(err->exc_type);
    free(err->message);
    free(err->traceback);
    memset(err, 0, sizeof(*err));
    err->block_index = -1;
}

const char *jlp7_error_kind_name(Jlp7ErrorKind kind) {
    switch (kind) {
        case JLP7_ERR_NONE:       return "none";
        case JLP7_ERR_CONFIG:     return "config";
        case JLP7_ERR_PY_COMPILE: return "python-compile";
        case JLP7_ERR_PY_RUNTIME: return "python-runtime";
        case JLP7_ERR_MARSHAL:    return "marshal";
        case JLP7_ERR_FOREIGN:    return "foreign";
        case JLP7_ERR_INTERNAL:   return "internal";
    }
    return "unknown";
}

/* Copy a Python str into a malloc'd C string, or NULL. Clears any error. */
static char *dup_pystr(PyObject *s) {
    if (!s) { PyErr_Clear(); return NULL; }
    const char *u = PyUnicode_AsUTF8(s);
    char *out = u ? strdup(u) : NULL;
    if (!u) PyErr_Clear();
    return out;
}

/* Return a new reference to obj.a.b (either name may be chained), or NULL. */
static PyObject *getattr2(PyObject *obj, const char *a, const char *b) {
    PyObject *first = PyObject_GetAttrString(obj, a);
    if (!first) { PyErr_Clear(); return NULL; }
    PyObject *second = PyObject_GetAttrString(first, b);
    Py_DECREF(first);
    if (!second) PyErr_Clear();
    return second;
}

/* Line, in the original source, of the innermost traceback frame that is
 * inside our block (its filename starts with "<jlp7:"). 0 if none. */
static int innermost_block_line(PyObject *exc) {
    int line = 0;
    PyObject *tb = PyException_GetTraceback(exc);
    while (tb && tb != Py_None) {
        PyObject *code = getattr2(tb, "tb_frame", "f_code");
        PyObject *fname = code ? PyObject_GetAttrString(code, "co_filename") : NULL;
        const char *f = fname ? PyUnicode_AsUTF8(fname) : NULL;
        if (f && strncmp(f, "<jlp7:", 6) == 0) {
            PyObject *ln = PyObject_GetAttrString(tb, "tb_lineno");
            if (ln) { line = (int)PyLong_AsLong(ln); Py_DECREF(ln); }
        }
        Py_XDECREF(fname);
        Py_XDECREF(code);
        PyErr_Clear();
        PyObject *next = PyObject_GetAttrString(tb, "tb_next");
        Py_DECREF(tb);
        tb = next;
    }
    Py_XDECREF(tb);
    PyErr_Clear();
    return line;
}

/* Turn the current Python exception into *err, and clear it. */
static void capture_exception(Jlp7Error *err, Jlp7ErrorKind kind) {
    PyObject *exc = PyErr_GetRaisedException();
    err->kind = kind;
    if (!exc) {
        err->message = strdup("(unknown Python error)");
        return;
    }

    PyObject *name = PyObject_GetAttrString((PyObject *)Py_TYPE(exc), "__name__");
    err->exc_type = dup_pystr(name);
    Py_XDECREF(name);

    PyObject *str = PyObject_Str(exc);
    err->message = dup_pystr(str);
    Py_XDECREF(str);

    /* Full formatted traceback: "".join(traceback.format_exception(exc)) */
    PyObject *tb_mod = PyImport_ImportModule("traceback");
    PyObject *lines  = tb_mod
        ? PyObject_CallMethod(tb_mod, "format_exception", "O", exc) : NULL;
    PyObject *empty  = PyUnicode_FromString("");
    PyObject *joined = (lines && empty) ? PyUnicode_Join(empty, lines) : NULL;
    err->traceback = dup_pystr(joined);
    Py_XDECREF(joined);
    Py_XDECREF(empty);
    Py_XDECREF(lines);
    Py_XDECREF(tb_mod);

    if (PyErr_GivenExceptionMatches((PyObject *)Py_TYPE(exc), PyExc_SyntaxError)) {
        PyObject *ln = PyObject_GetAttrString(exc, "lineno");
        if (ln && PyLong_Check(ln)) err->line = (int)PyLong_AsLong(ln);
        Py_XDECREF(ln);
        PyErr_Clear();
    } else {
        err->line = innermost_block_line(exc);
    }

    if (!err->message)
        err->message = strdup(err->exc_type ? err->exc_type : "(unknown Python error)");
    Py_DECREF(exc);
    PyErr_Clear();
}

static void set_marshal_error(Jlp7Error *err, const char *var, const char *why) {
    err->kind = JLP7_ERR_MARSHAL;
    size_t n = strlen(var) + strlen(why) + 32;
    err->message = malloc(n);
    if (err->message)
        snprintf(err->message, n, "variable '%s' cannot be exported: %s", var, why);
}

/* ── Jlp7Env → PyDict ───────────────────────────────────────────────── */

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
            case JLP7_ARRAY: {
                val = PyList_New((Py_ssize_t)v->arr_len);
                for (size_t k = 0; k < v->arr_len; k++) {
                    PyList_SET_ITEM(val, (Py_ssize_t)k,
                                     PyFloat_FromDouble(v->val.arr[k]));
                }
                break;
            }
        }
        if (val) {
            PyDict_SetItemString(d, v->name, val);
            Py_DECREF(val);
        } else {
            PyErr_Clear();
        }
    }
    return d;
}

/* ── PyDict → Jlp7Env ───────────────────────────────────────────────── */

/* Recursively flatten a Python list/tuple, or anything exposing
 * .tolist() (numpy arrays and similar), into a growable double
 * buffer. Nested sequences (e.g. a 2D confusion matrix) are flattened
 * in row-major order -- the caller only gets a flat array + count
 * back, same as everywhere else arrays cross this boundary.
 * Returns 0 on success, -1 if the object contains a non-numeric
 * element or a number that does not fit a double. Never leaves a
 * Python exception set. */
static int flatten_numeric(PyObject *obj, double **buf, size_t *len, size_t *cap) {
    if (!PyList_Check(obj) && !PyTuple_Check(obj) &&
        PyObject_HasAttrString(obj, "tolist")) {
        PyObject *as_list = PyObject_CallMethod(obj, "tolist", NULL);
        if (!as_list) { PyErr_Clear(); return -1; }
        int rc = flatten_numeric(as_list, buf, len, cap);
        Py_DECREF(as_list);
        return rc;
    }

    if (PyList_Check(obj) || PyTuple_Check(obj)) {
        Py_ssize_t n = PySequence_Size(obj);
        for (Py_ssize_t i = 0; i < n; i++) {
            PyObject *item = PySequence_GetItem(obj, i);
            if (!item) { PyErr_Clear(); return -1; }
            int rc = flatten_numeric(item, buf, len, cap);
            Py_DECREF(item);
            if (rc != 0) return -1;
        }
        return 0;
    }

    double v;
    if (PyBool_Check(obj))        v = PyObject_IsTrue(obj) ? 1.0 : 0.0;
    else if (PyLong_Check(obj))   v = PyLong_AsDouble(obj);
    else if (PyFloat_Check(obj))  v = PyFloat_AsDouble(obj);
    else return -1;
    if (PyErr_Occurred()) { PyErr_Clear(); return -1; }

    if (*len == *cap) {
        size_t ncap = (*cap == 0) ? 16 : (*cap * 2);
        double *nb = realloc(*buf, sizeof(double) * ncap);
        if (!nb) return -1;
        *buf = nb;
        *cap = ncap;
    }
    (*buf)[(*len)++] = v;
    return 0;
}

static int is_array_like(PyObject *obj) {
    if (PyList_Check(obj) || PyTuple_Check(obj)) return 1;
    if (PyUnicode_Check(obj) || PyBytes_Check(obj)) return 0;
    return PyObject_HasAttrString(obj, "tolist");
}

/* Read the public variables of d into `out` (a scratch env).
 * Returns 0 on success, -1 with *err set on a marshal error.
 *
 * A value with no env representation (dict, custom object, fitted model)
 * is left in Python: skipped when strict == 0, a marshal error when
 * strict == 1. A value that has a representation but does not fit it
 * (int outside long long, string with lone surrogates) is always an
 * error -- exporting a wrong value silently is worse than failing. */
static int export_locals(PyObject *d, Jlp7Env *out, int strict, Jlp7Error *err) {
    PyObject *key, *val;
    Py_ssize_t pos = 0;

    while (PyDict_Next(d, &pos, &key, &val)) {
        const char *name = PyUnicode_Check(key) ? PyUnicode_AsUTF8(key) : NULL;
        if (!name)                 { PyErr_Clear(); continue; }
        if (name[0] == '_')        continue;   /* skip dunder / private */
        if (PyCallable_Check(val)) continue;
        if (PyType_Check(val))     continue;
        if (PyModule_Check(val))   continue;

        if (PyBool_Check(val)) {
            /* Must check bool before long — bool is a subtype of int */
            jlp7_env_set_bool(out, name, PyObject_IsTrue(val));
        } else if (PyLong_Check(val)) {
            int overflow = 0;
            long long n = PyLong_AsLongLongAndOverflow(val, &overflow);
            if (overflow || (n == -1 && PyErr_Occurred())) {
                PyErr_Clear();
                set_marshal_error(err, name, "integer does not fit in long long");
                return -1;
            }
            jlp7_env_set_int(out, name, n);
        } else if (PyFloat_Check(val)) {
            jlp7_env_set_float(out, name, PyFloat_AsDouble(val));
        } else if (PyUnicode_Check(val)) {
            const char *s = PyUnicode_AsUTF8(val);
            if (!s) {
                PyErr_Clear();
                set_marshal_error(err, name, "string is not valid UTF-8");
                return -1;
            }
            jlp7_env_set_str(out, name, s);
        } else if (is_array_like(val)) {
            double *buf = NULL;
            size_t len = 0, cap = 0;
            if (flatten_numeric(val, &buf, &len, &cap) == 0 && len > 0) {
                jlp7_env_set_array(out, name, buf, len);
            } else if (strict) {
                free(buf);
                set_marshal_error(err, name, "not a non-empty list of numbers");
                return -1;
            }
            free(buf);
        } else if (strict) {
            char why[128];
            snprintf(why, sizeof(why), "no representation for type '%.60s'",
                     Py_TYPE(val)->tp_name);
            set_marshal_error(err, name, why);
            return -1;
        }
    }
    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────────── */

static int run_locked(const char *code, Jlp7Env *env, int line_offset,
                      int strict, Jlp7Error *err) {
    int rc = -1;

    /* Pad with blank lines so tracebacks and SyntaxErrors report lines of
     * the original source, not lines relative to the block. */
    if (line_offset < 1) line_offset = 1;
    size_t pad = (size_t)(line_offset - 1), clen = strlen(code);
    char *padded = malloc(pad + clen + 1);
    if (!padded) {
        err->kind = JLP7_ERR_INTERNAL;
        err->message = strdup("out of memory");
        return -1;
    }
    memset(padded, '\n', pad);
    memcpy(padded + pad, code, clen + 1);

    PyObject *builtins = PyImport_ImportModule("builtins");
    PyObject *globals  = PyDict_New();
    PyObject *locals   = env_to_pydict(env);
    PyObject *code_obj = NULL, *result = NULL;
    Jlp7Env  *scratch  = NULL;

    if (!builtins || !globals || !locals) {
        capture_exception(err, JLP7_ERR_INTERNAL);
        goto done;
    }
    PyDict_SetItemString(globals, "__builtins__", builtins);

    code_obj = Py_CompileString(padded, "<jlp7:python>", Py_file_input);
    if (!code_obj) {
        capture_exception(err, JLP7_ERR_PY_COMPILE);
        goto done;
    }

    result = PyEval_EvalCode(code_obj, globals, locals);
    if (!result) {
        capture_exception(err, JLP7_ERR_PY_RUNTIME);
        goto done;
    }

    /* Export into a scratch env first, so a marshal error halfway through
     * cannot leave the caller's env half-updated. */
    scratch = jlp7_env_new();
    if (!scratch) {
        err->kind = JLP7_ERR_INTERNAL;
        err->message = strdup("out of memory");
        goto done;
    }
    if (export_locals(locals, scratch, strict, err) != 0) goto done;

    jlp7_env_merge(env, scratch);
    rc = 0;

done:
    jlp7_env_free(scratch);
    Py_XDECREF(result);
    Py_XDECREF(code_obj);
    Py_XDECREF(locals);
    Py_XDECREF(globals);
    Py_XDECREF(builtins);
    free(padded);
    return rc;
}

int jlp7_run_python_ex(const char *code, Jlp7Env *env, int line_offset,
                       int strict, Jlp7Error *err) {
    Jlp7Error local;
    memset(&local, 0, sizeof(local));
    local.block_index = -1;

    ensure_python();
    PyGILState_STATE gil = PyGILState_Ensure();
    int rc = run_locked(code, env, line_offset, strict, &local);
    PyGILState_Release(gil);

    if (rc == 0) return 0;

    if (err) {
        jlp7_error_clear(err);
        *err = local;
    } else {
        fprintf(stderr, "[jlp7:python] %s error:\n%s\n",
                jlp7_error_kind_name(local.kind),
                local.traceback ? local.traceback
                                : (local.message ? local.message : "(no details)"));
        jlp7_error_clear(&local);
    }
    return -1;
}

int jlp7_run_python(const char *code, Jlp7Env *env) {
    return jlp7_run_python_ex(code, env, 1, 0, NULL);
}
