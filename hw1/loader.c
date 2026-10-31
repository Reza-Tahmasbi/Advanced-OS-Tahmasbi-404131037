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

    // an array to keep the ELF file first 4 bytess
    unsigned char magic[4];
    ssize_t bytes_read = read(fd, magic, sizeof(magic));

    // here we check of reading was successful
    if (bytes_read == -1){
        perror("read");
        close(fd);
        return 1;
    }

    // here we check if the ELF signiture has at least 4 bytes
    if (bytes_read < 4){
        fprintf(stderr, "Could not read the complete ELF signiture\n");
        close(fd);
        return 1;
    }

    // here we check if the signiture is as it is expected
    if (magic[0] == 0x7f && magic[1] == 'E' &&
        magic[2] == 'L' && magic[3] == 'F'){
        printf("ELF Signiture found");
    }else{
        fprintf(stderr, "ELF signiture not found\n");
        close(fd);
        return 1;
    }

    // if the file is not closed yet, close it(on success path)
    if (close(fd) == -1) {
        perror("close");
        return 1;
    }

    // return success
    return 0;
}