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

| C / Java       | Python  |
|----------------|---------|
| `long long`    | `int`   |
| `double`       | `float` |
| `int` (0/1)    | `bool`  |
| `char[]`       | `str`   |

## License

Apache 2.0 — see [LICENSE](LICENSE).  
Copyright 2026 Jean-Luc Robitaille.
