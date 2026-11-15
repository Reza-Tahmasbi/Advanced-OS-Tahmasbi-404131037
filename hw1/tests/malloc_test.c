#include <stdio.h>
#include <stdlib.h>

#define BUFFER_SIZE (1024 * 1024)
#define TEST_VALUE 0x5a

int main(void)
{
    unsigned char *buffer = malloc(BUFFER_SIZE);

    if (buffer == NULL) {
        perror("malloc");
        return 1;
    }
    for (size_t i=0; i<BUFFER_SIZE; i++){
        buffer[i] = TEST_VALUE;
    }
    for (size_t i=0; i<BUFFER_SIZE; i++){
        if (buffer[i] != TEST_VALUE){
            fprintf(stderr, "Incorrect value at index %zu\n", i);
            free(buffer);
            return 1;
        }
    }  
    free(buffer);
    printf("Malloc test passed\n");
    return 0;  
}