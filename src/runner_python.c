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
    size_t n = strlen(var) + strlen(why) + 64;
    err->message = malloc(n);
    if (err->message)
        snprintf(err->message, n, "variable '%s' cannot be exported: %s", var, why);
}

/* ── Jlp7Env → PyDict ───────────────────────────────────────────────── */

/* New reference, or NULL with a Python error set. */
static PyObject *var_to_py(const Jlp7Var *v) {
    switch (v->type) {
        case JLP7_INT:    return PyLong_FromLongLong(v->val.i);
        case JLP7_FLOAT:  return PyFloat_FromDouble(v->val.f);
        case JLP7_BOOL:   return PyBool_FromLong(v->val.b);
        case JLP7_STRING: return PyUnicode_FromString(v->val.s);
        case JLP7_NULL:   Py_RETURN_NONE;
        case JLP7_ARRAY: {
            PyObject *list = PyList_New((Py_ssize_t)v->arr_len);
            if (!list) return NULL;
            for (size_t k = 0; k < v->arr_len; k++) {
                PyObject *f = PyFloat_FromDouble(v->val.arr[k]);
                if (!f) { Py_DECREF(list); return NULL; }
                PyList_SET_ITEM(list, (Py_ssize_t)k, f);
            }
            return list;
        }
        case JLP7_LIST: {
            PyObject *list = PyList_New((Py_ssize_t)v->arr_len);
            if (!list) return NULL;
            for (size_t k = 0; k < v->arr_len; k++) {
                PyObject *item = var_to_py(&v->val.items[k]);
                if (!item) { Py_DECREF(list); return NULL; }
                PyList_SET_ITEM(list, (Py_ssize_t)k, item);
            }
            return list;
        }
        case JLP7_DICT: {
            PyObject *d = PyDict_New();
            if (!d) return NULL;
            for (size_t k = 0; k < v->arr_len; k++) {
                PyObject *item = var_to_py(&v->val.items[k]);
                if (!item || PyDict_SetItemString(d, v->val.items[k].name, item) != 0) {
                    Py_XDECREF(item);
                    Py_DECREF(d);
                    return NULL;
                }
                Py_DECREF(item);
            }
            return d;
        }
    }
    Py_RETURN_NONE;
}

static PyObject *env_to_pydict(const Jlp7Env *env) {
    PyObject *d = PyDict_New();
    for (size_t i = 0; i < env->count; i++) {
        const Jlp7Var *v = &env->vars[i];
        PyObject *val = var_to_py(v);
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

#define MAX_DEPTH 64

/* Recursively flatten a Python list/tuple, or anything exposing
 * .tolist() (numpy arrays and similar), into a growable double
 * buffer. Nested sequences (e.g. a 2D confusion matrix) are flattened
 * in row-major order -- the caller only gets a flat array + count
 * back, same as everywhere else arrays cross this boundary.
 * Returns 0 on success, -1 if the object contains a non-numeric
 * element or a number that does not fit a double. Never leaves a
 * Python exception set. */
static int flatten_numeric(PyObject *obj, double **buf, size_t *len, size_t *cap,
                           int depth) {
    if (depth > MAX_DEPTH) return -1;   /* too deep, or a cycle */
    if (!PyList_Check(obj) && !PyTuple_Check(obj) &&
        PyObject_HasAttrString(obj, "tolist")) {
        PyObject *as_list = PyObject_CallMethod(obj, "tolist", NULL);
        if (!as_list) { PyErr_Clear(); return -1; }
        int rc = flatten_numeric(as_list, buf, len, cap, depth + 1);
        Py_DECREF(as_list);
        return rc;
    }

    if (PyList_Check(obj) || PyTuple_Check(obj)) {
        Py_ssize_t n = PySequence_Size(obj);
        for (Py_ssize_t i = 0; i < n; i++) {
            PyObject *item = PySequence_GetItem(obj, i);
            if (!item) { PyErr_Clear(); return -1; }
            int rc = flatten_numeric(item, buf, len, cap, depth + 1);
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
    if (PyUnicode_Check(obj) || PyBytes_Check(obj) || PyDict_Check(obj)) return 0;
    return PyObject_HasAttrString(obj, "tolist");
}

/* Outcome of converting one Python value. */
enum { CONV_OK = 0, CONV_UNREP = 1, CONV_FATAL = 2 };

typedef struct {
    char why[160];
} Conv;

static int conv_note(Conv *c, int rc, const char *fmt, const char *arg) {
    snprintf(c->why, sizeof(c->why), fmt, arg ? arg : "");
    return rc;
}

/* Convert `obj` into `out` (which must be an unnamed JLP7_NULL value).
 *
 *   None                    -> NULL
 *   bool / int / float / str-> scalar
 *   list, tuple, ndarray    -> ARRAY if every leaf is a number (nesting is
 *                              flattened row-major, as it always was),
 *                              else LIST of converted items
 *   dict with str keys      -> DICT
 *
 * CONV_UNREP: no representation (set, object, non-str key, too deep).
 * CONV_FATAL: has one but it does not fit (int > long long, bad UTF-8).
 * On anything but CONV_OK, `out` is left cleared. */
static int py_to_var(PyObject *obj, Jlp7Var *out, int depth, Conv *c) {
    if (depth > MAX_DEPTH)
        return conv_note(c, CONV_UNREP, "nested too deeply (or contains itself)%s", "");

    if (obj == Py_None) { jlp7_var_set_null(out); return CONV_OK; }

    if (PyBool_Check(obj)) {
        jlp7_var_set_bool(out, PyObject_IsTrue(obj));
        return CONV_OK;
    }
    if (PyLong_Check(obj)) {
        int overflow = 0;
        long long n = PyLong_AsLongLongAndOverflow(obj, &overflow);
        if (overflow || (n == -1 && PyErr_Occurred())) {
            PyErr_Clear();
            return conv_note(c, CONV_FATAL, "integer does not fit in long long%s", "");
        }
        jlp7_var_set_int(out, n);
        return CONV_OK;
    }
    if (PyFloat_Check(obj)) {
        jlp7_var_set_float(out, PyFloat_AsDouble(obj));
        return CONV_OK;
    }
    if (PyUnicode_Check(obj)) {
        const char *str = PyUnicode_AsUTF8(obj);
        if (!str) {
            PyErr_Clear();
            return conv_note(c, CONV_FATAL, "string is not valid UTF-8%s", "");
        }
        jlp7_var_set_str(out, str);
        return CONV_OK;
    }

    if (PyDict_Check(obj)) {
        jlp7_var_set_dict(out);
        PyObject *key, *val;
        Py_ssize_t pos = 0;
        while (PyDict_Next(obj, &pos, &key, &val)) {
            const char *k = PyUnicode_Check(key) ? PyUnicode_AsUTF8(key) : NULL;
            if (!k) {
                PyErr_Clear();
                jlp7_var_clear(out);
                return conv_note(c, CONV_UNREP, "dict has a non-string key%s", "");
            }
            Jlp7Var *slot = jlp7_dict_put(out, k);
            int rc = slot ? py_to_var(val, slot, depth + 1, c) : CONV_FATAL;
            if (rc != CONV_OK) {
                jlp7_var_clear(out);
                if (!slot) return conv_note(c, CONV_FATAL, "out of memory%s", "");
                return rc;
            }
        }
        return CONV_OK;
    }

    if (is_array_like(obj)) {
        double *buf = NULL;
        size_t len = 0, cap = 0;
        if (flatten_numeric(obj, &buf, &len, &cap, depth) == 0 && len > 0) {
            jlp7_var_set_array(out, buf, len);
            free(buf);
            return CONV_OK;
        }
        free(buf);

        PyObject *seq = obj;
        int owned = 0;
        if (!PyList_Check(obj) && !PyTuple_Check(obj)) {
            seq = PyObject_CallMethod(obj, "tolist", NULL);
            if (!seq) {
                PyErr_Clear();
                return conv_note(c, CONV_UNREP, "tolist() failed%s", "");
            }
            owned = 1;
            if (!PyList_Check(seq) && !PyTuple_Check(seq)) {   /* 0-d array */
                int rc = py_to_var(seq, out, depth + 1, c);
                Py_DECREF(seq);
                return rc;
            }
        }
        jlp7_var_set_list(out);
        Py_ssize_t n = PySequence_Size(seq);
        int rc = CONV_OK;
        for (Py_ssize_t i = 0; i < n && rc == CONV_OK; i++) {
            PyObject *item = PySequence_GetItem(seq, i);
            Jlp7Var *slot = item ? jlp7_list_push(out) : NULL;
            if (!item || !slot) {
                PyErr_Clear();
                rc = conv_note(c, CONV_FATAL, "could not read list item%s", "");
            } else {
                rc = py_to_var(item, slot, depth + 1, c);
            }
            Py_XDECREF(item);
        }
        if (owned) Py_DECREF(seq);
        if (rc != CONV_OK) jlp7_var_clear(out);
        return rc;
    }

    return conv_note(c, CONV_UNREP, "no representation for type '%.60s'",
                     Py_TYPE(obj)->tp_name);
}

/* Read the public variables of d into `out` (a scratch env).
 * Returns 0 on success, -1 with *err set on a marshal error.
 *
 * A value with no env representation (custom object, fitted model, a dict
 * holding a set) is left in Python: skipped when strict == 0, a marshal
 * error when strict == 1. A value that has a representation but does not
 * fit it (int outside long long, string with lone surrogates) is always
 * an error -- exporting a wrong value silently is worse than failing.
 * A container is exported whole or not at all. */
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

        Jlp7Var tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.type = JLP7_NULL;
        Conv c = { "" };
        int rc = py_to_var(val, &tmp, 0, &c);

        if (rc == CONV_OK) {
            Jlp7Var *slot = jlp7_env_slot(out, name);
            if (slot) jlp7_var_move(slot, &tmp);
            jlp7_var_clear(&tmp);
        } else if (rc == CONV_FATAL || strict) {
            set_marshal_error(err, name, c.why);
            return -1;
        }
        /* CONV_UNREP, not strict: stays in Python. */
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
