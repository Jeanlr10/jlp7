# JLP7
**Jean-Luc's Practical Purposeful Pre-Processed Polyglot Python Project**

Run Java or C code with inline Python blocks. Variables flow bidirectionally across the boundary.

```c
int x = 5;
int y = 10;
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
git clone https://github.com/yourname/jlp7
cd jlp7
make
make test
```

## Usage

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

Link with:
```sh
gcc -Iinclude your_program.c -o your_program -L. -ljlp7 -lpython3.12
```

## Supported Types

| C / Java       | JLP7 internal | Python  |
|----------------|---------------|---------|
| `long long`    | `JLP7_INT`    | `int`   |
| `double`       | `JLP7_FLOAT`  | `float` |
| `int` (0/1)    | `JLP7_BOOL`   | `bool`  |
| `char[]`       | `JLP7_STRING` | `str`   |

## License

Apache 2.0 — see [LICENSE](LICENSE).  
Copyright 2026 Jean-Luc Robitaille.
