# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jean-Luc Robitaille

"""
setup.py for JLP7.

Builds libjlp7.so from the C sources, copies it into the Python package
directory, then installs the package normally.

Usage:
    pip install .
    pip install -e .   # editable / in-tree
"""

import os
import subprocess
import shutil
from setuptools import setup, find_packages
from setuptools.command.build_py import build_py
from setuptools.command.develop import develop

REPO_ROOT   = os.path.dirname(os.path.abspath(__file__))
LIB_SRC     = os.path.join(REPO_ROOT, "..")       # Makefile lives here
LIB_NAME    = "libjlp7.so"
PKG_DIR     = os.path.join(REPO_ROOT, "jlp7")


def build_native():
    """Run `make lib` in the repo root to produce libjlp7.so."""
    print("Building libjlp7.so...")
    result = subprocess.run(
        ["make", "lib"],
        cwd=LIB_SRC,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            "`make lib` failed. Make sure gcc and Python 3.12 dev headers "
            "are installed."
        )

    so_src = os.path.join(LIB_SRC, LIB_NAME)
    so_dst = os.path.join(PKG_DIR, LIB_NAME)

    if not os.path.exists(so_src):
        raise RuntimeError(
            f"{LIB_NAME} not found at {so_src} after `make lib`."
        )

    shutil.copy2(so_src, so_dst)
    print(f"Copied {LIB_NAME} → {so_dst}")


class BuildWithNative(build_py):
    def run(self):
        build_native()
        super().run()


class DevelopWithNative(develop):
    def run(self):
        build_native()
        super().run()


setup(
    name             = "jlp7",
    version          = "0.1.0",
    description      = "Jean-Luc's Practical Purposeful Pre-Processed Polyglot Python Project",
    author           = "Jean-Luc Robitaille",
    license          = "Apache-2.0",
    packages         = find_packages(),
    package_data     = {"jlp7": ["libjlp7.so"]},
    python_requires  = ">=3.10",
    cmdclass         = {
        "build_py": BuildWithNative,
        "develop":  DevelopWithNative,
    },
)
