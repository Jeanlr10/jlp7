# JLP7
**Jean-Luc's Practical Purposeful Pre-Processed Polyglot Python Project**

Run C or Java code with inline Python blocks. Variables flow bidirectionally across the boundary. Core written in C for performance, with a Python package for convenience.

```c
long long x = 5;
long long y = 10;
/p
x = x + 1
result = x * y
p/
printf("result = %lld\n", result);  // result = 60
```

## Dependencies

- GCC
- Python 3.12+ (with dev headers)
- Java 9+ with `jshell` on PATH (for Java support)

## Build

```sh
git clone https://github.com/jeanlr10/jlp7
cd jlp7
make
make test
```

## Usage

### Python

```sh
pip install ./python
```

```python
from jlp7 import JLP7

pg  = JLP7('c')  # or 'java'
env = pg.run('''
    long long x = 5;
    /p
    x = x * 2
    label = "doubled"
    p/
    printf("%s: %lld\n", label, x);
''')
print(env)  # {'x': 10, 'label': 'doubled'}
```

Pre-seed variables from Python:

```python
env = pg.run('/p\nx = x * 2\np/\n', env={'x': 21})
# env['x'] == 42
```

### C

```c
#include "jlp7.h"

Jlp7Config cfg = jlp7_default_config("c");  // or "java"
Jlp7Env   *env = jlp7_env_new();

jlp7_exec(
    "long long x = 42;\n"
    "/p\n"
    "x = x * 2\n"
    "p/\n"
    "printf(\"%lld\\n\", x);\n",
    &cfg, env);

jlp7_env_free(env);
```

```sh
make lib
gcc -Iinclude your_program.c -o your_program -L. -ljlp7 -lpython3.12
```

## Errors

A failed run never leaves partial state: if any block fails, `env` is
restored to what it was before the call.

```python
from jlp7 import JLP7, JLP7Error

try:
    JLP7('c').run('long long x = 1;\n/p\ny = 1 / 0\np/\n')
except JLP7Error as e:
    e.kind         # 'python-runtime'
    e.exc_type     # 'ZeroDivisionError'
    e.line         # 3  (line in your source, not in the block)
    e.block_index  # 1
    e.traceback    # full Python traceback
```

Kinds: `python-compile`, `python-runtime`, `marshal`, `foreign`,
`config`, `internal`. In C, use `jlp7_exec_ex(source, &cfg, env, &err)`
and `jlp7_error_clear(&err)`.

A Python integer that does not fit `long long` is always a `marshal`
error. Values with no C/Java form (dicts, objects) stay in Python and are
skipped, unless you set `strict` (`JLP7('c', strict=True)`, or
`cfg.strict = 1`), which makes them a `marshal` error too.

## Threads

Any number of threads can call `jlp7_exec` at once, as long as each uses
its own `Jlp7Env`. Python blocks take the GIL and run one at a time; C and
Java blocks run in child processes and run in parallel. Do not share one
`Jlp7Env` between threads without your own lock. The full rules are in the
"Threading" comment in `include/jlp7.h`. From Python, `JLP7.run()` is safe
to call from many `threading.Thread`s.

```sh
make test-threads   # 16 threads, mixed pass/fail blocks, plus C blocks
```

## Supported Types

| C / Java                        | Python              | Env type       |
|---------------------------------|---------------------|----------------|
| `long long`, `int`, `long`      | `int`               | `JLP7_INT`     |
| `double`, `float`               | `float`             | `JLP7_FLOAT`   |
| `int` (0/1), `boolean`, `bool`  | `bool`              | `JLP7_BOOL`    |
| `char[]`, `String`              | `str`               | `JLP7_STRING`  |
| `double[N]`, `int[]`, ...       | `list` of numbers   | `JLP7_ARRAY`   |
| `List<...>`, mixed arrays       | `list` / `tuple`    | `JLP7_LIST`    |
| `Map<String, ...>`, C structs   | `dict` (str keys)   | `JLP7_DICT`    |
| `null`                          | `None`              | `JLP7_NULL`    |

## Structured data

Lists and dicts nest to any depth (64 levels; a value that contains
itself is rejected).

```python
env = JLP7('java').run('''
    /p
    cfg = {'name': 'run1', 'layers': [{'units': 8}, {'units': 16}], 'opt': None}
    p/
    List<Object> layers = (List<Object>) cfg.get("layers");
    cfg.put("depth", layers.size());
''')
env['cfg']['depth']   # 2
```

**Numeric lists are arrays.** A list, tuple or `ndarray` whose leaves are
all numbers becomes one flat `JLP7_ARRAY` of doubles, as before; nesting
is flattened row-major, so a 2x2 matrix arrives as 4 numbers. Any other
list (a `str`, `None`, a dict, ... among the items) is a `JLP7_LIST`
and keeps its shape.

**What Java sees.** Env lists and dicts appear as `List<Object>` and
`Map<String, Object>`; integers inside them are `Long`, floats `Double`,
numeric lists `double[]`. Declare `List<...>`, `Map<...>`, `ArrayList`,
`HashMap`, `LinkedHashMap`, `TreeMap` or an array such as `int[]` in a
block and it goes back to the env.

**Integer size in Java.** An env integer is declared `int` if it fits in
32 bits, `long` otherwise. So `int doubled = counter * 2;` compiles for a
small counter. The price: `int` arithmetic can overflow where Python's
would not, and a `long` value cannot be assigned to such a variable
without a cast.

**What C sees: `// jlp7:export`.** C has no run-time type information, so
a struct is exported only when you mark its definition:

```c
// jlp7:export
struct Point { double x, y; };

// jlp7:export
typedef struct {
    char   name[16];       // string
    int    id;
    bool   alive;
    struct Point pos;      // nested struct   -> nested dict
    double hist[3];        // number array    -> list of numbers
    struct Point trail[2]; // struct array    -> list of dicts
} Entity;

struct Point p = {1.5, 2.5};   // exported as {'x': 1.5, 'y': 2.5}
Entity e;                      // no initialiser: filled from the env
                               // dict "e", if Python made one
```

- Variables of a marked type are exported as dicts; arrays of them as
  lists of dicts.
- A struct declared **without** an initialiser is filled from the env
  dict of the same name. With an initialiser, your value wins (the same
  rule as for a redeclared `int`).
- The C struct is the source of truth: dict keys that are not fields are
  dropped, missing keys leave the field alone.
- Pointers, multi-dimensional arrays, nested struct bodies and unions are
  not supported; a marked struct that uses them is an error.
- Env lists and dicts with no struct variable in the block pass through
  a C block untouched.

## Notes on the wire format

C and Java blocks print one `__VARS__:{...}` JSON line, which the library
reads back. `NaN`, `Infinity` and printf's `nan`/`inf` are accepted.
A block that prints no such line, or one that is malformed, is an error.

## License

Apache 2.0 — see [LICENSE](LICENSE).  
Copyright 2026 Jean-Luc Robitaille.
