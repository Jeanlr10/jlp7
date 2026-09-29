CC      = gcc
CFLAGS  = -pthread -Wall -Wextra -std=c11 -Iinclude -MMD -MP \
          $(shell python3-config --includes)
LDFLAGS = -pthread $(shell python3-config --embed --ldflags 2>/dev/null || \
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
DEPS    = $(OBJ:.o=.d) src/main.d src/thread_test.d
TEST    = jlp7_test
THREADT = jlp7_thread_test
LIB     = libjlp7.so

.PHONY: all test test-threads lib clean

all: $(TEST)

$(TEST): $(OBJ) src/main.o
	$(CC) -o $@ $^ $(LDFLAGS)

$(THREADT): $(OBJ) src/thread_test.o
	$(CC) -o $@ $^ $(LDFLAGS)

lib: $(OBJ)
	$(CC) -shared -fPIC -o $(LIB) $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

test: $(TEST)
	./$(TEST)

test-threads: $(THREADT)
	./$(THREADT) --with-c --with-java

-include $(DEPS)

clean:
	rm -f $(OBJ) $(DEPS) src/main.o src/thread_test.o $(TEST) $(THREADT) $(LIB)
