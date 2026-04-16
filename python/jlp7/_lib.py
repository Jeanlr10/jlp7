# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jean-Luc Robitaille

"""
jlp7._lib — ctypes bindings for libjlp7.so.

This module is internal. Use the JLP7 class in jlp7/__init__.py instead.
"""

import ctypes
import ctypes.util
import os
import sys

# ── Locate libjlp7.so ────────────────────────────────────────────────────────

def _find_lib() -> ctypes.CDLL:
    # RTLD_GLOBAL is critical: libjlp7.so embeds libpython. Loading it with
    # RTLD_GLOBAL tells the dynamic linker to share the already-loaded Python
    # symbols rather than initialising a second interpreter, which would segfault.
    flags = ctypes.RTLD_GLOBAL

    # 1. Same directory as this file (editable / in-tree install)
    here = os.path.dirname(os.path.abspath(__file__))
    candidate = os.path.join(here, "libjlp7.so")
    if os.path.exists(candidate):
        return ctypes.CDLL(candidate, mode=flags)

    # 2. One level up (repo root after `make lib`)
    candidate = os.path.abspath(os.path.join(here, "..", "libjlp7.so"))
    if os.path.exists(candidate):
        return ctypes.CDLL(candidate, mode=flags)

    # 3. System library path
    name = ctypes.util.find_library("jlp7")
    if name:
        return ctypes.CDLL(name, mode=flags)

    raise OSError(
        "libjlp7.so not found. Run `make lib` in the repo root first, "
        "or install via `pip install .`"
    )

_lib = _find_lib()

# ── C struct mirrors ─────────────────────────────────────────────────────────

# enum Jlp7Type
JLP7_INT    = 0
JLP7_FLOAT  = 1
JLP7_BOOL   = 2
JLP7_STRING = 3

class _ValUnion(ctypes.Union):
    _fields_ = [
        ("i", ctypes.c_longlong),
        ("f", ctypes.c_double),
        ("b", ctypes.c_int),
        ("s", ctypes.c_char_p),
    ]

class _Jlp7Var(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("type", ctypes.c_int),
        ("val",  _ValUnion),
    ]

class _Jlp7Env(ctypes.Structure):
    _fields_ = [
        ("vars",  ctypes.POINTER(_Jlp7Var)),
        ("count", ctypes.c_size_t),
        ("cap",   ctypes.c_size_t),
    ]

class _Jlp7Config(ctypes.Structure):
    _fields_ = [
        ("language", ctypes.c_char_p),
        ("allowpy",  ctypes.c_int),
        ("debug",    ctypes.c_int),
    ]

# ── Function signatures ──────────────────────────────────────────────────────

_lib.jlp7_env_new.restype        = ctypes.POINTER(_Jlp7Env)
_lib.jlp7_env_new.argtypes       = []

_lib.jlp7_env_free.restype       = None
_lib.jlp7_env_free.argtypes      = [ctypes.POINTER(_Jlp7Env)]

_lib.jlp7_env_set_int.restype    = None
_lib.jlp7_env_set_int.argtypes   = [ctypes.POINTER(_Jlp7Env), ctypes.c_char_p, ctypes.c_longlong]

_lib.jlp7_env_set_float.restype  = None
_lib.jlp7_env_set_float.argtypes = [ctypes.POINTER(_Jlp7Env), ctypes.c_char_p, ctypes.c_double]

_lib.jlp7_env_set_bool.restype   = None
_lib.jlp7_env_set_bool.argtypes  = [ctypes.POINTER(_Jlp7Env), ctypes.c_char_p, ctypes.c_int]

_lib.jlp7_env_set_str.restype    = None
_lib.jlp7_env_set_str.argtypes   = [ctypes.POINTER(_Jlp7Env), ctypes.c_char_p, ctypes.c_char_p]

_lib.jlp7_env_get.restype        = ctypes.POINTER(_Jlp7Var)
_lib.jlp7_env_get.argtypes       = [ctypes.POINTER(_Jlp7Env), ctypes.c_char_p]

_lib.jlp7_exec.restype           = ctypes.c_int
_lib.jlp7_exec.argtypes          = [ctypes.c_char_p,
                                     ctypes.POINTER(_Jlp7Config),
                                     ctypes.POINTER(_Jlp7Env)]

_lib.jlp7_default_config.restype  = _Jlp7Config
_lib.jlp7_default_config.argtypes = [ctypes.c_char_p]
