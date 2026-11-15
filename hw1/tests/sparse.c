#include <stdio.h>
#include <string.h>

#define BUFFER_SIZE (64 * 1024 * 1024)
#define FIRST_VALUE 0x5a
#define LAST_VALUE 0xa5

unsigned char buffer[BUFFER_SIZE];

static int print_memory_usage(void)
{
    FILE *file = fopen("/proc/self/status", "r");

    if (file == NULL) {
        perror("fopen status");
        return 1;
    }

    char line[256];
    int fields = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "VmSize:", sizeof("VmSize:") - 1) == 0 ||
            strncmp(line, "VmRSS:", sizeof("VmRSS:") - 1) == 0) {
            printf("%s", line);
            fields++;
        }
    }

    if (ferror(file)) {
        fprintf(stderr, "Could not read process memory information\n");
        fclose(file);
        return 1;
    }

    if (fclose(file) == EOF) {
        perror("fclose status");
        return 1;
    }

    if (fields != 2) {
        fprintf(stderr, "Missing process memory information\n");
        return 1;
    }

    return 0;
}

int main(void)
{
    volatile unsigned char *pointer = buffer;

    if (pointer[0] != 0 || pointer[BUFFER_SIZE - 1] != 0){
        fprintf(stderr, "Initial sparse values are not zero\n");
        return 1;
    }

    pointer[0] = FIRST_VALUE;
    pointer[BUFFER_SIZE - 1] = LAST_VALUE;

    if (pointer[0] != FIRST_VALUE ||
        pointer[BUFFER_SIZE - 1] != LAST_VALUE){
        fprintf(stderr, "Sparse values are incorrect\n");
        return 1;
    }

    if (print_memory_usage() != 0) {
        return 1;
    }
    printf("Sparse test passed\n");
    return 0;
}