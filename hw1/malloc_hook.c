#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

static void *(*real_malloc)(size_t) = NULL;

static void print_number(uintmax_t value, unsigned int base)
{
    const char digits[] = "0123456789abcdef";
    char buffer[sizeof(value) * 8];
    size_t position = sizeof(buffer);

    do {
        buffer[--position] = digits[value % base];
        value /= base;
    } while (value != 0);

    write(STDERR_FILENO, buffer + position,
          sizeof(buffer) - position);
}

void *malloc(size_t size)
{
    if (real_malloc == NULL) {
        real_malloc = dlsym(RTLD_NEXT, "malloc");

        if (real_malloc == NULL) {
            const char message[] = "Could not find real malloc\n";
            write(STDERR_FILENO, message, sizeof(message) - 1);
            _exit(1);
        }
    }

    write(STDERR_FILENO, "malloc size=", sizeof("malloc size=") - 1);
    print_number(size, 10);
    write(STDERR_FILENO, "\n", 1);

    void *buffer = real_malloc(size);

    if (buffer != NULL) {
        memset(buffer, 0xcc, size);
    }

    write(STDERR_FILENO, "malloc returned=0x",
          sizeof("malloc returned=0x") - 1);
    print_number((uintptr_t)buffer, 16);
    write(STDERR_FILENO, "\n", 1);

    return buffer;
}
