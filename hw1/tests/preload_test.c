#include <stdio.h>
#include <stdlib.h>

#define BUFFER_SIZE 64
#define MAGIC 0xcc

int main(void)
{
    unsigned char *buffer = malloc(BUFFER_SIZE);

    if (buffer == NULL) {
        perror("malloc");
        return 1;
    }

    for (size_t i = 0; i < BUFFER_SIZE; i++) {
        if (buffer[i] != MAGIC) {
            fprintf(stderr, "Unexpected value at index %zu\n", i);
            free(buffer);
            return 1;
        }
    }

    free(buffer);
    printf("Preload test passed\n");
    return 0;
}