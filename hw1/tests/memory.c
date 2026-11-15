#include <stdio.h>

#ifndef BUFFER_SIZE
#define BUFFER_SIZE (1024 * 1024)
#endif

#define TEST_VALUE 0x5a

int initial_value = 123;
unsigned char buffer[BUFFER_SIZE];

int main (void)
{
    if (initial_value != 123){
        fprintf(stderr, "Initial data value is incorrect\n");
        return 1;
    }
    for (size_t i=0; i<sizeof(buffer); i++){
        if (buffer[i] != 0){
            fprintf(stderr, "BSS is not zero at index %zu\n", i);
            return 1;
        }
    }
    for (size_t i=0; i<sizeof(buffer); i++){
        buffer[i] = TEST_VALUE;
    }
    for (size_t i=0; i<sizeof(buffer); i++){
        if (buffer[i] != TEST_VALUE){
            fprintf(stderr, "Written value is incorrect at index %zu\n", i);
            return 1;
        }
    }
    printf("Memory test passed\n");
    return 0;

}