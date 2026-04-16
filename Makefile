CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -Iinclude \
          $(shell python3-config --includes)
LDFLAGS = $(shell python3-config --embed --ldflags 2>/dev/null || \
                  python3-config --ldflags) \
          -lpython3.12

SRC = src/env.c          \
      src/parser.c       \
      src/runner_python.c \
      src/java_json.c    \
      src/java_builder.c \
      src/runner_java.c  \
      src/c_builder.c    \
      src/c_json.c       \
      src/runner_c.c     \
      src/jlp7.c

OBJ     = $(SRC:.c=.o)
TEST    = jlp7_test
LIB     = libjlp7.so

.PHONY: all test lib clean

all: $(TEST)

$(TEST): $(OBJ) src/main.o
	$(CC) -o $@ $^ $(LDFLAGS)

lib: $(OBJ)
	$(CC) -shared -fPIC -o $(LIB) $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

test: $(TEST)
	./$(TEST)

clean:
	rm -f $(OBJ) src/main.o $(TEST) $(LIB)
