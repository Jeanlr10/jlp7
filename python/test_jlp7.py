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
        pg.run("", env={"x": {1, 2}})
        fail("should have raised TypeError")
    except TypeError as e:
        ok("TypeError raised for a set value")
        assert_true("'x'" in str(e), "message names the variable")


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


def test_error_fields():
    print("\n── Errors: structured JLP7Error ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return
    pg = JLP7("c")
    try:
        pg.run("long long x = 1;\n/p\nx = 2\ny = 1 / 0\np/\n")
        fail("expected JLP7Error")
    except JLP7Error as e:
        assert_eq(e.kind, "python-runtime", "kind")
        assert_eq(e.exc_type, "ZeroDivisionError", "exc_type")
        assert_eq(e.line, 4, "line in original source")
        assert_eq(e.block_index, 1, "block_index")
        assert_true("Traceback" in (e.traceback or ""), "traceback present")

    try:
        pg.run("long long x = 1;\n\n/p\nx = = 2\np/\n")
        fail("expected JLP7Error")
    except JLP7Error as e:
        assert_eq(e.kind, "python-compile", "syntax error kind")
        assert_eq(e.line, 4, "syntax error line")


def test_strict_marshal():
    print("\n── Errors: strict marshalling ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return
    src = "long long x = 1;\n/p\nd = {1, 2}\np/\n"
    env = JLP7("c").run(src)
    assert_true("d" not in env, "non-strict leaves set in Python")
    try:
        JLP7("c", strict=True).run(src)
        fail("expected JLP7Error")
    except JLP7Error as e:
        assert_eq(e.kind, "marshal", "strict raises marshal error")


def test_threads():
    print("\n── Threads: JLP7 from many Python threads ──")
    import threading
    results = {}

    def work(tid):
        pg = JLP7("c")
        env = {"n": 0, "tid": tid}
        try:
            for _ in range(50):
                env = pg.run("/p\nn = n + tid\np/\n", env=env)
            try:
                pg.run("/p\nn = 0\nraise KeyError('k')\np/\n", env=env)
                results[tid] = "no error raised"
                return
            except JLP7Error as e:
                if e.exc_type != "KeyError":
                    results[tid] = f"wrong error {e.exc_type}"
                    return
            results[tid] = env["n"]
        except Exception as e:  # noqa: BLE001
            results[tid] = f"exception {e!r}"

    threads = [threading.Thread(target=work, args=(t,)) for t in range(1, 9)]
    for t in threads: t.start()
    for t in threads: t.join()
    for tid in range(1, 9):
        assert_eq(results.get(tid), 50 * tid, f"thread {tid} result")


def test_nested_env_in():
    print("\n── Nested: Python dict/list -> and back ──")
    env_in = {
        "cfg": {"name": "run1", "layers": [{"units": 8}, {"units": 16}],
                "opt": None, "lr": [0.1, 0.01], "deep": {"a": {"b": [1, "x", True]}}},
        "names": ["a", "b"],
        "mixed": (1, "two", None, 3.5),
        "matrix": [[1, 2], [3, 4]],
        "empty": [],
        "flag": True,
        "small": 5,
    }
    env = JLP7("c").run("", env=env_in)
    assert_eq(env["cfg"]["layers"], [{"units": 8}, {"units": 16}], "list of dicts")
    assert_eq(env["cfg"]["opt"], None, "None round-trips")
    assert_eq(env["cfg"]["lr"], [0.1, 0.01], "numeric list inside a dict")
    assert_eq(env["cfg"]["deep"]["a"]["b"], [1, "x", True], "deeply nested mixed list")
    assert_eq(env["names"], ["a", "b"], "list of str")
    assert_eq(env["mixed"], [1, "two", None, 3.5], "tuple -> list, None kept")
    assert_eq(env["matrix"], [1.0, 2.0, 3.0, 4.0], "numeric 2D list flattens (unchanged)")
    assert_eq(env["empty"], [], "empty list")
    assert_eq(env["flag"], True, "bool still a bool")
    assert_true(type(env["small"]) is int, "int still an int")


def test_nested_python_block():
    print("\n── Nested: values made in a /p block ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return
    env = JLP7("c").run(
        "long long n = 3;\n"
        "/p\n"
        "hist = [{'epoch': i, 'loss': 1.0 / (i + 1)} for i in range(n)]\n"
        "summary = {'best': min(h['loss'] for h in hist), 'tags': ['a', 'b']}\n"
        "p/\n")
    assert_eq(len(env["hist"]), 3, "list of 3 dicts")
    assert_eq(env["hist"][2]["epoch"], 2, "hist[2].epoch")
    assert_eq(env["summary"]["tags"], ["a", "b"], "summary.tags")
    assert_true(abs(env["summary"]["best"] - 1 / 3) < 1e-12, "float precision kept")


def test_nested_bad_input():
    print("\n── Nested: unsupported input ──")
    pg = JLP7("c")
    try:
        pg.run("", env={"d": {1: "a"}})
        fail("expected TypeError")
    except TypeError as e:
        assert_true("'d'" in str(e) and "str" in str(e), "int dict key: TypeError names var")
    try:
        pg.run("", env={"big": 2 ** 70})
        fail("expected ValueError")
    except ValueError:
        ok("int above long long: ValueError")
    cyc = []
    cyc.append(cyc)
    try:
        pg.run("", env={"c": cyc})
        fail("expected ValueError")
    except ValueError:
        ok("self-containing list: ValueError, no crash")
    deep = "leaf"
    for _ in range(100):
        deep = [deep]
    try:
        pg.run("", env={"d": deep})
        fail("expected ValueError")
    except ValueError:
        ok("100-deep nesting: ValueError")


def test_c_struct_export():
    print("\n── C structs (// jlp7:export) ──")
    if not has_gcc():
        print("  ⚠ gcc not found — skipping")
        return
    src = (
        "// jlp7:export\n"
        "struct Point { double x, y; };\n"
        "struct Point p = {1.5, 2.5};\n"
        "struct Point q;\n"
        "q.y = q.y + 1;\n"
        "/p\n"
        "dist2 = p['x'] ** 2 + p['y'] ** 2\n"
        "p/\n"
    )
    env = JLP7("c").run(src, env={"q": {"x": 10.0, "y": 20.0}})
    assert_eq(env["p"], {"x": 1.5, "y": 2.5}, "struct -> dict")
    assert_eq(env["q"], {"x": 10.0, "y": 21.0}, "dict -> struct -> dict")
    assert_eq(env["dist2"], 1.5 ** 2 + 2.5 ** 2, "Python used the struct")

    try:
        JLP7("c").run("// jlp7:export\nstruct B { char *name; };\n")
        fail("expected JLP7Error")
    except JLP7Error:
        ok("pointer field is an error")


def test_java_nested():
    print("\n── Java: nested values ──")
    if not has_jshell():
        print("  ⚠ jshell not found — skipping")
        return
    env = JLP7("java").run(
        "/p\n"
        "cfg = {'name': 'x', 'sizes': [1, 2, 3], 'note': None}\n"
        "small = 5\n"
        "p/\n"
        "Map<String, Object> extra = new LinkedHashMap<>();\n"
        "extra.put(\"k\", \"v\\\"q\");\n"
        "cfg.put(\"added\", extra);\n"
        "int twice = small * 2;\n")
    assert_eq(env["cfg"]["added"], {"k": 'v"q'}, "Java put a Map into a Python dict")
    assert_eq(env["cfg"]["note"], None, "null survived")
    assert_eq(env["cfg"]["sizes"], [1.0, 2.0, 3.0], "double[] came back")
    assert_eq(env["twice"], 10, "int typing: 'int twice = small * 2'")


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
    test_error_fields()
    test_strict_marshal()
    test_threads()
    test_nested_env_in()
    test_nested_python_block()
    test_nested_bad_input()
    test_c_struct_export()
    test_java_nested()
    test_java_basic()
    test_java_string()

    print(f"\n══════════════════════════════════")
    print(f"  Passed: {passed}  |  Failed: {failed}")
    print(f"══════════════════════════════════")
    sys.exit(1 if failed else 0)
