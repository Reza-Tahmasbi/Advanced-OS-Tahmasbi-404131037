CC = gcc
CFLAGS = -Wall -Wextra -g
LOADER_FLAGS = -fPIE -pie -Wl,-z,now
TEST_FLAGS = -static -fno-pie -no-pie -Wl,-z,norelro

.PHONY: all clean

all: build/apager build/dpager build/hpager build/hello-static build/memory-static build/malloc-static build/fault-static build/readonly-static build/malloc-hook.so build/preload-test build/sparse-static build/memory-large-static build/hello-dynamic build/hpager-one build/hpager-three

build:
	mkdir -p build

build/apager: loader.c | build
	$(CC) $(CFLAGS) $(LOADER_FLAGS) $< -o $@

build/dpager: loader.c | build
	$(CC) $(CFLAGS) $(LOADER_FLAGS) -DDEMAND_PAGING $< -o $@

build/hpager: loader.c | build
	$(CC) $(CFLAGS) $(LOADER_FLAGS) -DDEMAND_PAGING -DHYBRID_PAGING $< -o $@

build/hello-static: tests/hello.c | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) $< -o $@

build/memory-static: tests/memory.c | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) $< -o $@

build/malloc-static: tests/malloc_test.c | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) $< -o $@

build/fault-static: tests/fault.c | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) $< -o $@

build/readonly-static: tests/readonly.c | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) $< -o $@

build/malloc-hook.so: malloc_hook.c | build
	$(CC) $(CFLAGS) -shared -fPIC $< -o $@ -ldl

build/preload-test: tests/preload_test.c | build
	$(CC) $(CFLAGS) $< -o $@

build/sparse-static: tests/sparse.c | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) $< -o $@

build/memory-large-static: tests/memory.c | build
	$(CC) $(CFLAGS) $(TEST_FLAGS) -DBUFFER_SIZE=67108864 $< -o $@

build/hello-dynamic: tests/hello_dynamic.c | build
	$(CC) $< -o $@

build/hpager-one: loader.c | build
	$(CC) $(CFLAGS) $(LOADER_FLAGS) -DDEMAND_PAGING -DHYBRID_PAGING -DHYBRID_PAGES_PER_FAULT=1 $< -o $@

build/hpager-three: loader.c | build
	$(CC) $(CFLAGS) $(LOADER_FLAGS) -DDEMAND_PAGING -DHYBRID_PAGING -DHYBRID_PAGES_PER_FAULT=3 $< -o $@

clean:
	rm -f build/apager build/dpager build/hpager build/hello-static build/memory-static build/malloc-static build/fault-static build/readonly-static build/malloc-hook.so build/preload-test build/sparse-static build/memory-large-static build/hello-dynamic build/hpager-one build/hpager-three