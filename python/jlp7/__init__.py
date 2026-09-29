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


def _env_to_dict(env_ptr) -> dict[str, Any]:
    """Read a Jlp7Env* into a plain Python dict."""
    env = env_ptr.contents
    result: dict[str, Any] = {}
    for i in range(env.count):
        v = env.vars[i]
        name = v.name.decode()
        if v.type == JLP7_INT:
            result[name] = v.val.i
        elif v.type == JLP7_FLOAT:
            result[name] = v.val.f
        elif v.type == JLP7_BOOL:
            result[name] = bool(v.val.b)
        elif v.type == JLP7_STRING:
            result[name] = v.val.s.decode() if v.val.s else ""
        elif v.type == JLP7_ARRAY:
            n = v.arr_len
            result[name] = [v.val.arr[k] for k in range(n)] if n else []
    return result


def _dict_to_env(d: dict[str, Any], env_ptr) -> None:
    """Write a Python dict into an existing Jlp7Env*."""
    for name, value in d.items():
        key = name.encode()
        if isinstance(value, bool):
            _lib.jlp7_env_set_bool(env_ptr, key, int(value))
        elif isinstance(value, int):
            _lib.jlp7_env_set_int(env_ptr, key, value)
        elif isinstance(value, float):
            _lib.jlp7_env_set_float(env_ptr, key, value)
        elif isinstance(value, str):
            _lib.jlp7_env_set_str(env_ptr, key, value.encode())
        elif isinstance(value, (list, tuple)) or hasattr(value, "tolist"):
            # accepts plain lists/tuples and numpy arrays (via .tolist());
            # flattened, so a 2D array goes in row-major and comes back flat
            flat = _flatten(value)
            arr_t = ctypes.c_double * len(flat)
            _lib.jlp7_env_set_array(env_ptr, key, arr_t(*flat), len(flat))
        else:
            raise TypeError(
                f"Variable '{name}' has unsupported type {type(value).__name__}. "
                f"JLP7 supports int, float, bool, str, and list/tuple/ndarray "
                f"of numbers."
            )


def _flatten(value) -> list[float]:
    """Flatten a list/tuple/ndarray of numbers into a flat list of floats."""
    if hasattr(value, "tolist") and not isinstance(value, (list, tuple)):
        value = value.tolist()
    out: list[float] = []
    for item in value:
        if isinstance(item, (list, tuple)) or (
            hasattr(item, "tolist") and not isinstance(item, (int, float))
        ):
            out.extend(_flatten(item))
        else:
            out.append(float(item))
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
            Initial variable state. Keys must be str, values must be
            int, float, bool, or str.

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
