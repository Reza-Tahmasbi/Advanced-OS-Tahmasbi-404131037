#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdio.h>
#include <fcntl.h> //to open a file
#include <unistd.h> //to close a file
#include <elf.h>
#include <sys/stat.h>
#include <stdint.h>

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

    // an array to keep the ELF file first 16 bytes
    unsigned char magic[EI_NIDENT];
    ssize_t bytes_read = read(fd, magic, sizeof(magic));

    // here we check of reading was successful

    if (bytes_read == -1) {
        perror("read");
        close(fd);
        return 1;
    }

    if (bytes_read != EI_NIDENT){
        fprintf(stderr, "Could not read the complete ELF identification\n");
        close(fd);
        return 1;
    }

    // here we check if the signiture is as it is expected
    if (magic[0] == 0x7f && magic[1] == 'E' &&
        magic[2] == 'L' && magic[3] == 'F'){
        printf("ELF Signiture found\n");
    }else{
        fprintf(stderr, "ELF signiture not found\n");
        close(fd);
        return 1;
    }

    // check the class of the file
    if (magic[EI_CLASS] == ELFCLASS64){
        printf("ELF64 detected\n");
    }
    else if (magic[EI_CLASS] == ELFCLASS32){
        fprintf(stderr, "ELF32 is not supported in this loader\n");
        close(fd);
        return 1;
    }
    else{
        fprintf(stderr, "any other value");
        close(fd);
        return 1;
    }

    if (magic[EI_DATA] != ELFDATA2LSB){
        fprintf(stderr, "Unsupported ELF byte order\n");
        close(fd);
        return 1;
    }

    // move back to the begining of the file
    if (lseek(fd, 0, SEEK_SET) == -1){
        perror("lseek");
        close(fd);
        return 1;
    }

    // read the complete ELF64 header
    Elf64_Ehdr header;
    bytes_read = read(fd, &header, sizeof(header));

    if (bytes_read == -1) {
        perror("read");
        close(fd);
        return 1;
    } 

    if (bytes_read != (ssize_t)sizeof(header)) {
        fprintf(stderr, "Could not read the complete ELF header\n");
        close(fd);
        return 1;
    }

    if (header.e_machine != EM_X86_64){
        fprintf(stderr, "Unsupported ELF machine\n");
        close(fd);
        return 1;
    }

    if (header.e_version != EV_CURRENT || magic[EI_VERSION] != EV_CURRENT){
        fprintf(stderr, "Unsupported ELF version\n");
        close(fd);
        return 1;  
    }

    if (header.e_ehsize != sizeof(header)) {
        fprintf(stderr, "Unexpected ELF header size\n");
        close(fd);
        return 1;
    }

    if (header.e_phentsize != sizeof(Elf64_Phdr)) {
        fprintf(stderr, "Unexpected program header entry size\n");
        close(fd);
        return 1;
    }

    struct stat file_info;

    if (fstat(fd, &file_info) == -1){
        perror("fstat");
        close(fd);
        return 1;
    }

    if (!S_ISREG(file_info.st_mode) || file_info.st_size < 0) {
        fprintf(stderr, "Expected a regular file with a valid size\n");
        close(fd);
        return 1;
    }

    Elf64_Xword file_size = (Elf64_Xword)file_info.st_size;

    if (header.e_phoff > file_size ||
        header.e_phnum >
            (file_size - header.e_phoff) / sizeof(Elf64_Phdr)) {
        fprintf(stderr, "Program header table is outside the file\n");
        close(fd);
        return 1;
    }

    // print information from the header
    printf("Entry point: 0x%llx\n",
        (unsigned long long)header.e_entry);

    printf("Program header offset: %llu\n",
        (unsigned long long)header.e_phoff);

    printf("Program header count: %u\n",
        (unsigned int)header.e_phnum);

    if (lseek(fd, (off_t)header.e_phoff, SEEK_SET) == -1){
        perror("lseek");
        close(fd);
        return 1;
    }

    long page_size_result = sysconf(_SC_PAGESIZE);

    if (page_size_result <= 0) {
        fprintf(stderr, "Could not determine page size\n");
        close(fd);
        return 1;
    }

    Elf64_Xword page_size = (Elf64_Xword)page_size_result;

    for (unsigned int i = 0; i< header.e_phnum; i++){

        Elf64_Phdr segment;
        bytes_read = read(fd, &segment, sizeof(segment));

        // check the read result here
        if (bytes_read == -1) {
            perror("read");
            close(fd);
            return 1;
        }

        if (bytes_read != (ssize_t)sizeof(segment)) {
            fprintf(stderr, "Could not read the complete program header entry\n");
            close(fd);
            return 1;
        }

        // inspect segment.p_type here
        if (segment.p_type == PT_LOAD) {
            if (segment.p_filesz > segment.p_memsz) {
                fprintf(stderr, "Segment file size exceeds memory size\n");
                close(fd);
                return 1;
            }

            if (segment.p_offset > file_size ||
                segment.p_filesz > file_size - segment.p_offset) {
                fprintf(stderr, "Segment data is outside the file\n");
                close(fd);
                return 1;
            }

            if (segment.p_memsz > UINT64_MAX - segment.p_vaddr) {
                fprintf(stderr, "Segment virtual address range overflows\n");
                close(fd);
                return 1;
            }

            if (segment.p_memsz == 0) {
                continue;
            }
            if (segment.p_vaddr % page_size != segment.p_offset % page_size) {
                fprintf(stderr, "Segment address and file offset are not page-congruent\n");
                close(fd);
                return 1;
            }
            Elf64_Addr map_start =
                segment.p_vaddr - segment.p_vaddr % page_size;

            Elf64_Off map_offset =
                segment.p_offset - segment.p_offset % page_size;

            Elf64_Addr segment_end =
                segment.p_vaddr + segment.p_memsz;

            Elf64_Xword padding =
                (page_size - segment_end % page_size) % page_size;

            if (segment_end > UINT64_MAX - padding) {
                fprintf(stderr, "Page-aligned segment range overflows\n");
                close(fd);
                return 1;
            }

            Elf64_Xword map_length =
                segment_end + padding - map_start;
            printf("LOAD offset=0x%llx vaddr=0x%llx "
                "filesz=0x%llx memsz=0x%llx flags=0x%x\n",
                (unsigned long long)segment.p_offset,
                (unsigned long long)segment.p_vaddr,
                (unsigned long long)segment.p_filesz,
                (unsigned long long)segment.p_memsz,
                (unsigned int)segment.p_flags);

            printf("PLAN start=0x%llx offset=0x%llx length=0x%llx\n",
                (unsigned long long)map_start,
                (unsigned long long)map_offset,
                (unsigned long long)map_length);

                if (header.e_type == ET_EXEC &&
                segment.p_offset == 0 &&
                segment.p_filesz >= SELFMAG &&
                map_length == page_size) {

                    void *requested = (void *)(uintptr_t)map_start;

                    void *mapped = mmap(requested, (size_t)map_length, PROT_READ,
                                        MAP_PRIVATE | MAP_FIXED_NOREPLACE,
                                        fd, (off_t)map_offset);

                    if (mapped == MAP_FAILED) {
                        perror("mmap");
                        close(fd);
                        return 1;
                    }

                    if (mapped != requested) {
                        fprintf(stderr, "Expected mapping at %p, got %p\n",
                                requested, mapped);
                        munmap(mapped, (size_t)map_length);
                        close(fd);
                        return 1;
                    }

                    const unsigned char *bytes = mapped;

                    printf("Mapped at %p, first bytes: %02x %02x %02x %02x\n",
                        mapped,
                        (unsigned int)bytes[0], (unsigned int)bytes[1],
                        (unsigned int)bytes[2], (unsigned int)bytes[3]);

                    if (munmap(mapped, (size_t)map_length) == -1) {
                            perror("munmap");
                            close(fd);
                            return 1;
                        }
                }

 

        }

    }
    // if the file is not closed yet, close it(on success path)
    if (close(fd) == -1) {
        perror("close");
        return 1;
    }

    // return success
    return 0;
}