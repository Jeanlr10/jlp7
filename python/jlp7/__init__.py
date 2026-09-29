# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jean-Luc Robitaille

"""
jlp7 — Jean-Luc's Practical Purposeful Pre-Processed Polyglot Python Project

Run C or Java code with inline /p...p/ Python blocks.
Variables flow bidirectionally across the language boundary.

Example::

    from jlp7 import JLP7

    pg = JLP7('c')
    env = pg.run('''
        long long x = 5;
        long long y = 10;
        /p
        x = x + 1
        result = x * y
        p/
        printf("result = %lld\\n", result);
    ''')
    print(env)  # {'x': 6, 'y': 10, 'result': 60}
"""

from __future__ import annotations

import ctypes
from typing import Any

from ._lib import (
    _lib,
    _Jlp7Config,
    _Jlp7Error,
    _ERR_KINDS,
    JLP7_INT, JLP7_FLOAT, JLP7_BOOL, JLP7_STRING, JLP7_ARRAY,
    JLP7_LIST, JLP7_DICT, JLP7_NULL,
)

__version__ = "0.2.0"
__all__     = ["JLP7", "JLP7Error"]


class JLP7Error(Exception):
    """
    Raised when the underlying C library returns an error.

    Attributes
    ----------
    kind : str
        ``'python-compile'``, ``'python-runtime'``, ``'marshal'``,
        ``'foreign'``, ``'config'`` or ``'internal'``.
    exc_type : str | None
        Name of the Python exception class (``'ZeroDivisionError'``), if any.
    message : str
        Short message.
    traceback : str | None
        Full formatted Python traceback. Line numbers refer to the source
        string passed to ``run()``.
    block_index : int
        0-based index of the failing block, or -1.
    line : int
        1-based line in the source, or 0 if unknown.
    """

    def __init__(self, message, kind="internal", exc_type=None,
                 traceback=None, block_index=-1, line=0):
        super().__init__(message)
        self.kind        = kind
        self.exc_type    = exc_type
        self.message     = message
        self.traceback   = traceback
        self.block_index = block_index
        self.line        = line

    def __str__(self):
        where = f" (line {self.line})" if self.line else ""
        head  = f"{self.exc_type}: " if self.exc_type else ""
        return f"[{self.kind}]{where} {head}{self.message}"


def _cstr(ptr):
    return ctypes.string_at(ptr).decode("utf-8", "replace") if ptr else None


def _error_from_struct(err: _Jlp7Error) -> JLP7Error:
    return JLP7Error(
        _cstr(err.message) or "unknown error",
        kind        = _ERR_KINDS.get(err.kind, "internal"),
        exc_type    = _cstr(err.exc_type),
        traceback   = _cstr(err.traceback),
        block_index = err.block_index,
        line        = err.line,
    )


_MAX_DEPTH = 64
_INT_MIN, _INT_MAX = -(2 ** 63), 2 ** 63 - 1


def _var_to_py(v, depth: int = 0) -> Any:
    """Read one Jlp7Var (any type, any nesting) into a Python value."""
    if depth > _MAX_DEPTH:
        raise JLP7Error("value nested too deeply", kind="marshal")
    t = v.type
    if t == JLP7_INT:
        return v.val.i
    if t == JLP7_FLOAT:
        return v.val.f
    if t == JLP7_BOOL:
        return bool(v.val.b)
    if t == JLP7_STRING:
        return v.val.s.decode("utf-8", "replace") if v.val.s else ""
    if t == JLP7_ARRAY:
        n = v.arr_len
        return [v.val.arr[k] for k in range(n)] if n else []
    if t == JLP7_LIST:
        return [_var_to_py(v.val.items[k], depth + 1) for k in range(v.arr_len)]
    if t == JLP7_DICT:
        return {
            v.val.items[k].name.decode("utf-8", "replace"):
                _var_to_py(v.val.items[k], depth + 1)
            for k in range(v.arr_len)
        }
    return None   # JLP7_NULL


def _env_to_dict(env_ptr) -> dict[str, Any]:
    """Read a Jlp7Env* into a plain Python dict."""
    env = env_ptr.contents
    return {
        env.vars[i].name.decode(): _var_to_py(env.vars[i])
        for i in range(env.count)
    }


def _as_sequence(value):
    """list/tuple as is; anything with .tolist() (ndarray, numpy scalar)
    is converted. Returns None for everything else."""
    if isinstance(value, (list, tuple)):
        return value
    if isinstance(value, (str, bytes, dict)):
        return None
    if hasattr(value, "tolist"):
        out = value.tolist()
        return out if isinstance(out, (list, tuple)) else _Scalar(out)
    return None


class _Scalar:
    """A 0-d array converted to its Python scalar."""
    def __init__(self, value):
        self.value = value


def _is_numeric_tree(value, depth: int = 0) -> bool:
    """True if every leaf of a (nested) sequence is a number or bool."""
    if depth > _MAX_DEPTH:
        return False
    seq = _as_sequence(value)
    if seq is None:
        return isinstance(value, (int, float))
    if isinstance(seq, _Scalar):
        return isinstance(seq.value, (int, float))
    return all(_is_numeric_tree(item, depth + 1) for item in seq)


def _fill_var(var, value, depth: int = 0) -> None:
    """Write a Python value into a Jlp7Var* (creating children as needed).

    None -> NULL; bool/int/float/str -> scalars; dict with str keys -> DICT;
    list/tuple/ndarray -> a flat ARRAY when every leaf is a number
    (nesting is flattened row-major), else a LIST.
    """
    if depth > _MAX_DEPTH:
        raise ValueError("value is nested too deeply (or contains itself)")

    if value is None:
        _lib.jlp7_var_set_null(var)
    elif isinstance(value, bool):
        _lib.jlp7_var_set_bool(var, int(value))
    elif isinstance(value, int):
        if not _INT_MIN <= value <= _INT_MAX:
            raise ValueError("integer does not fit in long long")
        _lib.jlp7_var_set_int(var, value)
    elif isinstance(value, float):
        _lib.jlp7_var_set_float(var, value)
    elif isinstance(value, str):
        _lib.jlp7_var_set_str(var, value.encode("utf-8"))
    elif isinstance(value, dict):
        _lib.jlp7_var_set_dict(var)
        for key, item in value.items():
            if not isinstance(key, str):
                raise TypeError(
                    f"dict keys must be str, got {type(key).__name__}"
                )
            _fill_var(_lib.jlp7_dict_put(var, key.encode("utf-8")), item, depth + 1)
    else:
        seq = _as_sequence(value)
        if seq is None:
            raise TypeError(f"unsupported type {type(value).__name__}")
        if isinstance(seq, _Scalar):
            _fill_var(var, seq.value, depth + 1)
        elif seq and _is_numeric_tree(seq):
            flat = _flatten(seq)
            arr_t = ctypes.c_double * len(flat)
            _lib.jlp7_var_set_array(var, arr_t(*flat), len(flat))
        else:
            _lib.jlp7_var_set_list(var)
            for item in seq:
                _fill_var(_lib.jlp7_list_push(var), item, depth + 1)


def _dict_to_env(d: dict[str, Any], env_ptr) -> None:
    """Write a Python dict into an existing Jlp7Env*."""
    for name, value in d.items():
        slot = _lib.jlp7_env_slot(env_ptr, name.encode())
        try:
            _fill_var(slot, value)
        except (TypeError, ValueError) as exc:
            kind = TypeError if isinstance(exc, TypeError) else ValueError
            raise kind(
                f"Variable '{name}': {exc}. JLP7 supports None, int, float, "
                f"bool, str, dict with str keys, and lists/tuples/ndarrays "
                f"of those."
            ) from None


def _flatten(value) -> list[float]:
    """Flatten a list/tuple/ndarray of numbers into a flat list of floats."""
    seq = _as_sequence(value)
    if seq is None:
        return [float(value)]
    if isinstance(seq, _Scalar):
        return [float(seq.value)]
    out: list[float] = []
    for item in seq:
        out.extend(_flatten(item))
    return out


class JLP7:
    """
    Polyglot execution engine.

    Parameters
    ----------
    language : str
        Target language for foreign blocks. ``'c'`` or ``'java'``.
    allowpy : bool
        Whether ``/p...p/`` blocks are permitted. Default ``True``.
    debug : bool
        Enable verbose internal logging from the C library. Default ``False``.
    strict : bool
        Raise ``JLP7Error`` (kind ``'marshal'``) when a Python block leaves a
        variable that cannot cross the boundary (dict, object, non-numeric
        list). Default ``False``: such variables stay in Python, unexported.

    Examples
    --------
    Basic C usage::

        pg = JLP7('c')
        env = pg.run('''
            long long x = 10;
            /p
            x = x * 3
            label = "tripled"
            p/
            printf("%s: %lld\\n", label, x);
        ''')
        # env == {'x': 30, 'label': 'tripled'}

    Pre-seeding variables::

        pg = JLP7('c')
        env = pg.run('printf("%lld\\n", x);', env={'x': 42})

    Persistent state across multiple run() calls::

        pg = JLP7('c')
        env = pg.run('long long counter = 0;')
        env = pg.run('/p\\ncounter += 1\\np/\\n', env=env)
    """

    def __init__(
        self,
        language: str,
        allowpy: bool = True,
        debug: bool = False,
        strict: bool = False,
    ) -> None:
        lang = language.lower()
        if lang not in ("c", "java"):
            raise ValueError(
                f"Unsupported language: '{language}'. Choose 'c' or 'java'."
            )
        self._language = lang
        self._allowpy  = allowpy
        self._debug    = debug
        self._strict   = strict

    def run(
        self,
        source: str,
        env: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        """
        Execute a JLP7 polyglot source string.

        Parameters
        ----------
        source : str
            The source code to execute, optionally containing ``/p...p/`` blocks.
        env : dict, optional
            Initial variable state. Keys must be str. Values may be None,
            int, float, bool, str, a dict with str keys, or a
            list/tuple/ndarray of those (nesting allowed).

        Returns
        -------
        dict
            All variables in scope after execution.

        Raises
        ------
        JLP7Error
            If the C library reports an error (compile failure, runtime
            error, unsupported language, etc.). The exception carries
            ``kind``, ``exc_type``, ``traceback``, ``block_index`` and
            ``line``. A failed ``run()`` never returns partial state.
        TypeError
            If ``env`` contains values of unsupported types.
        """
        # Build Jlp7Config
        cfg = _Jlp7Config(
            language = self._language.encode(),
            allowpy  = int(self._allowpy),
            debug    = int(self._debug),
            strict   = int(self._strict),
        )

        # Build Jlp7Env, seed with any provided vars
        env_ptr = _lib.jlp7_env_new()
        try:
            if env:
                _dict_to_env(env, env_ptr)

            err = _Jlp7Error()
            rc = _lib.jlp7_exec_ex(
                source.encode(),
                ctypes.byref(cfg),
                env_ptr,
                ctypes.byref(err),
            )

            if rc != 0:
                try:
                    exc = _error_from_struct(err)
                finally:
                    _lib.jlp7_error_clear(ctypes.byref(err))
                raise exc

            return _env_to_dict(env_ptr)

        finally:
            _lib.jlp7_env_free(env_ptr)
