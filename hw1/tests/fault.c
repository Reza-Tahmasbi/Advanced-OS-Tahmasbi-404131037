#include <stddef.h>

int main(void)
{
    volatile int *pointer = NULL;
    return *pointer;
}