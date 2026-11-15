CC = gcc
CFLAGS = -Wall -Wextra -g
LOADER_FLAGS = -fPIE -pie
TEST_FLAGS = -static -fno-pie -no-pie

.PHONY: all clean

all: build/apager build/dpager build/hello-static build/memory-static build/malloc-static

build:
	mkdir -p build

build/apager: loader.c | build
	$(CC) $(CFLAGS) $(LOADER_FLAGS) $< -o $@

build/dpager: loader.c | build
	$(CC) $(CFLAGS) $(LOADER_FLAGS) -DDEMAND_PAGING $< -o $@

build/hello-static: tests/hello.c | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) $< -o $@

build/memory-static: tests/memory.c | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) $< -o $@

build/malloc-static: tests/malloc_test.c | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) $< -o $@

clean:
	rm -f build/apager build/dpager build/hello-static build/memory-static build/malloc-static