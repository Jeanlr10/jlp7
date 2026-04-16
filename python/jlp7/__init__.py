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
    JLP7_INT, JLP7_FLOAT, JLP7_BOOL, JLP7_STRING,
)

__version__ = "0.1.0"
__all__     = ["JLP7", "JLP7Error"]


class JLP7Error(Exception):
    """Raised when the underlying C library returns an error."""


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
        else:
            raise TypeError(
                f"Variable '{name}' has unsupported type {type(value).__name__}. "
                f"JLP7 supports int, float, bool, and str."
            )


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
    ) -> None:
        lang = language.lower()
        if lang not in ("c", "java"):
            raise ValueError(
                f"Unsupported language: '{language}'. Choose 'c' or 'java'."
            )
        self._language = lang
        self._allowpy  = allowpy
        self._debug    = debug

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
            error, unsupported language, etc.).
        TypeError
            If ``env`` contains values of unsupported types.
        """
        # Build Jlp7Config
        cfg = _Jlp7Config(
            language = self._language.encode(),
            allowpy  = int(self._allowpy),
            debug    = int(self._debug),
        )

        # Build Jlp7Env, seed with any provided vars
        env_ptr = _lib.jlp7_env_new()
        try:
            if env:
                _dict_to_env(env, env_ptr)

            rc = _lib.jlp7_exec(
                source.encode(),
                ctypes.byref(cfg),
                env_ptr,
            )

            if rc != 0:
                raise JLP7Error(
                    f"jlp7_exec failed (rc={rc}). "
                    f"Check stderr for details, or re-run with debug=True."
                )

            return _env_to_dict(env_ptr)

        finally:
            _lib.jlp7_env_free(env_ptr)
