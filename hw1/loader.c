#include <stdio.h>
#include <fcntl.h> //to open a file
#include <unistd.h> //to close a file

// a simple file path reader
int main (int argc, char *argv[])
{
    if (argc != 2){
        fprintf(stderr, "Usage: %s <program>\n", argv[0]);
        return 1;
    }
    // open for read_only, fd is the file descriptor
    int fd = open(argv[1], O_RDONLY);

    // if file opening fails
    if (fd == -1){
        perror("open");
        return 1;
    }

    printf("Target file: %s\n", argv[1]);

    // close the opened file
    close(fd);
    return 0;
}