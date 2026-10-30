#include <stdio.h>

# a simple file path reader
int main (int argc, char *argv[])
{
    if (argc != 2){
        fprintf(stderr, "Usage: %s <program>\n", argv[0]);
        return 1;
    }

    printf("Target file: %s\n", argv[1]);
    return 0;
}