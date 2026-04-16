# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Jean-Luc Robitaille

"""
test_jlp7.py — Python binding tests for JLP7.

Run with:  python test_jlp7.py
Requires:  libjlp7.so built (`make lib` in repo root)
           gcc on PATH for C tests
           jshell on PATH for Java tests
"""

import sys
import os
import shutil

# Allow running from python/ directory without installing
sys.path.insert(0, os.path.dirname(__file__))

from jlp7 import JLP7, JLP7Error

passed = 0
failed = 0


def ok(msg):
    global passed
    print(f"  ✓ {msg}")
    passed += 1


def fail(msg):
    global failed
    print(f"  ✗ {msg}")
    failed += 1


def assert_eq(a, b, msg):
    if a == b:
        ok(msg)
    else:
        fail(f"{msg}  (got {a!r}, expected {b!r})")


def assert_true(cond, msg):
    if cond:
        ok(msg)
    else:
        fail(msg)


def has_gcc():
    return shutil.which("gcc") is not None


def has_jshell():
    return shutil.which("jshell") is not None


# ── API / init tests ─────────────────────────────────────────────────────────

def test_invalid_language():
    print("\n── Invalid language ──")
    try:
        JLP7("cobol")
        fail("should have raised ValueError")
    except ValueError:
        ok("ValueError on unsupported language")


def test_allowpy_false():
    print("\n── allowpy=False ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return
    pg = JLP7('c', allowpy=False)
    try:
        pg.run("/p\nx = 1\np/\n")
        fail("should have raised JLP7Error")
    except JLP7Error:
        ok("JLP7Error raised when allowpy=False")


# ── C tests ──────────────────────────────────────────────────────────────────

def test_c_basic():
    print("\n── C: basic round-trip ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return

    pg  = JLP7('c')
    env = pg.run(
        "long long x = 5;\n"
        "long long y = 10;\n"
        "/p\n"
        "x = x + 1\n"
        "result = x * y\n"
        "p/\n"
        'printf("result = %lld\\n", result);\n'
    )
    assert_eq(env.get("x"),      6,  "x = 6")
    assert_eq(env.get("result"), 60, "result = 60")


def test_c_pre_seeded_env():
    print("\n── C: pre-seeded env ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return

    pg  = JLP7('c')
    env = pg.run(
        "/p\nx = x * 2\np/\n",
        env={"x": 21}
    )
    assert_eq(env.get("x"), 42, "x = 42")


def test_c_string():
    print("\n── C: string round-trip ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return

    pg  = JLP7('c')
    env = pg.run(
        "/p\ngreeting = 'hello'\np/\n"
        'printf("%s\\n", greeting);\n'
    )
    assert_eq(env.get("greeting"), "hello", "string passed through")


def test_c_bool():
    print("\n── C: bool round-trip ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return

    pg  = JLP7('c')
    env = pg.run("/p\nflag = True\np/\n", env={"flag": False})
    assert_true(env.get("flag") is True, "bool flipped to True")


def test_c_multi_block():
    print("\n── C: multi-block pipeline ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return

    pg  = JLP7('c')
    env = pg.run(
        "long long counter = 0;\n"
        "/p\ncounter = counter + 10\np/\n"
        "long long doubled = counter * 2;\n"
        "/p\nfinal_val = doubled + 1\np/\n"
    )
    assert_eq(env.get("counter"),   10, "counter = 10")
    assert_eq(env.get("doubled"),   20, "doubled = 20")
    assert_eq(env.get("final_val"), 21, "final_val = 21")


def test_c_compile_error():
    print("\n── C: compile error surfaces as JLP7Error ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return

    pg = JLP7('c')
    try:
        pg.run("this is not valid C!!!\n")
        fail("should have raised JLP7Error")
    except JLP7Error:
        ok("JLP7Error raised on compile failure")


def test_type_error():
    print("\n── Python: unsupported env type raises TypeError ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return

    pg = JLP7('c')
    try:
        pg.run("", env={"x": [1, 2, 3]})
        fail("should have raised TypeError")
    except TypeError:
        ok("TypeError raised for list value")


# ── Java tests ───────────────────────────────────────────────────────────────

def test_java_basic():
    print("\n── Java: basic round-trip ──")
    if not has_jshell():
        print("  ⚠ jshell not found — skipping")
        return

    pg  = JLP7('java')
    env = pg.run(
        "int x = 5;\n"
        "int y = 10;\n"
        "/p\n"
        "x = x + 1\n"
        "result = x * y\n"
        "p/\n"
        'System.out.println("result = " + result);\n'
    )
    assert_eq(env.get("x"),      6,  "x = 6")
    assert_eq(env.get("result"), 60, "result = 60")


def test_java_string():
    print("\n── Java: string round-trip ──")
    if not has_jshell():
        print("  ⚠ jshell not found — skipping")
        return

    pg  = JLP7('java')
    env = pg.run(
        'String greeting = "hello";\n'
        "/p\n"
        "greeting = greeting.upper() + ' WORLD'\n"
        "p/\n"
        "System.out.println(greeting);\n"
    )
    assert_eq(env.get("greeting"), "HELLO WORLD", "string mutated")


# ── main ─────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("JLP7 Python bindings — test suite")
    print("══════════════════════════════════")

    test_invalid_language()
    test_allowpy_false()
    test_c_basic()
    test_c_pre_seeded_env()
    test_c_string()
    test_c_bool()
    test_c_multi_block()
    test_c_compile_error()
    test_type_error()
    test_java_basic()
    test_java_string()

    print(f"\n══════════════════════════════════")
    print(f"  Passed: {passed}  |  Failed: {failed}")
    print(f"══════════════════════════════════")
    sys.exit(1 if failed else 0)
