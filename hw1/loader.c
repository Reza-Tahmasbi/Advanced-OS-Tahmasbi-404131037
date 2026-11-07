#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdio.h>
#include <fcntl.h> //to open a file
#include <unistd.h> //to close a file
#include <elf.h>
#include <sys/stat.h>
#include <stdint.h>
#include <string.h>
#include <sys/auxv.h>

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

    Elf64_Addr target_phdr = 0;
    int found_phdr = 0;
    Elf64_Xword phdr_size =
    (Elf64_Xword)header.e_phnum * header.e_phentsize;

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

            if (header.e_type == ET_EXEC) {
                int prot = 0;
                if (segment.p_flags & PF_R) {
                    prot |= PROT_READ;
                }
                if (segment.p_flags & PF_W) {
                    prot |= PROT_WRITE;
                }
                if (segment.p_flags & PF_X) {
                    prot |= PROT_EXEC;
                }

                Elf64_Addr map_end = map_start + map_length;
                Elf64_Addr file_end = segment.p_vaddr + segment.p_filesz;
                Elf64_Addr file_map_end = map_start;
                Elf64_Xword zero_length = 0;

                if (segment.p_filesz > 0) {
                    Elf64_Xword file_padding =
                        (page_size - file_end % page_size) % page_size;
                    file_map_end = file_end + file_padding;

                    Elf64_Addr zero_end = file_map_end;
                    if (zero_end > segment_end) {
                        zero_end = segment_end;
                    }
                    zero_length = zero_end - file_end;
                }

                Elf64_Xword file_length = file_map_end - map_start;
                Elf64_Xword anon_length = map_end - file_map_end;

                if (file_length > 0) {
                    int file_prot = prot;
                    if (zero_length > 0) {
                        file_prot = PROT_READ | PROT_WRITE;
                    }

                    void *requested = (void *)(uintptr_t)map_start;
                    void *mapped = mmap(requested, (size_t)file_length,
                                        file_prot,
                                        MAP_PRIVATE | MAP_FIXED_NOREPLACE,
                                        fd, (off_t)map_offset);

                    if (mapped == MAP_FAILED) {
                        perror("mmap file");
                        close(fd);
                        return 1;
                    }
                    if (mapped != requested) {
                        fprintf(stderr, "Expected mapping at %p, got %p\n",
                                requested, mapped);
                        munmap(mapped, (size_t)file_length);
                        close(fd);
                        return 1;
                    }

                    fprintf(stderr,
                            "Mapped address=%p offset=0x%llx length=0x%llx\n",
                            mapped,
                            (unsigned long long)map_offset,
                            (unsigned long long)file_length);

                    if (zero_length > 0) {
                        memset((void *)(uintptr_t)file_end, 0,
                               (size_t)zero_length);
                    }

                    if (file_prot != prot &&
                        mprotect(mapped, (size_t)file_length, prot) == -1) {
                        perror("mprotect");
                        close(fd);
                        return 1;
                    }
                }

                if (anon_length > 0) {
                    void *requested = (void *)(uintptr_t)file_map_end;
                    void *mapped = mmap(requested, (size_t)anon_length, prot,
                                        MAP_PRIVATE | MAP_ANONYMOUS |
                                        MAP_FIXED_NOREPLACE,
                                        -1, 0);

                    if (mapped == MAP_FAILED) {
                        perror("mmap anonymous");
                        close(fd);
                        return 1;
                    }
                    if (mapped != requested) {
                        fprintf(stderr, "Expected mapping at %p, got %p\n",
                                requested, mapped);
                        munmap(mapped, (size_t)anon_length);
                        close(fd);
                        return 1;
                    }

                    fprintf(stderr,
                            "Mapped address=%p offset=N/A length=0x%llx\n",
                            mapped, (unsigned long long)anon_length);
                }
                if (!found_phdr && (segment.p_flags & PF_R) &&
                    header.e_phoff >= segment.p_offset) {
                    Elf64_Off delta = header.e_phoff - segment.p_offset;

                    if (delta <= segment.p_filesz &&
                        phdr_size <= segment.p_filesz - delta) {
                        target_phdr = segment.p_vaddr + delta;
                        found_phdr = 1;
                    }
                }
            }
        }
    }
    // if the file is not closed yet, close it(on success path)
    if (close(fd) == -1) {
        perror("close");
        return 1;
    }

    if (header.e_type == ET_EXEC) {
        extern char **environ;
        const size_t stack_size = 1024 * 1024;
        const size_t stack_alignment = 16;
        if (!found_phdr) {
            fprintf(stderr, "Program header table is not in a readable LOAD segment\n");
            return 1;
        }

        uintptr_t random_addr = getauxval(AT_RANDOM);
        if (random_addr == 0) {
            fprintf(stderr, "Missing AT_RANDOM\n");
            return 1;
        }

        uintptr_t auxv[][2] = {
            { AT_PHDR, target_phdr },
            { AT_PHENT, header.e_phentsize },
            { AT_PHNUM, header.e_phnum },
            { AT_PAGESZ, page_size },
            { AT_BASE, 0 },
            { AT_ENTRY, header.e_entry },
            { AT_UID, getauxval(AT_UID) },
            { AT_EUID, getauxval(AT_EUID) },
            { AT_GID, getauxval(AT_GID) },
            { AT_EGID, getauxval(AT_EGID) },
            { AT_SECURE, getauxval(AT_SECURE) },
            { AT_HWCAP, getauxval(AT_HWCAP) },
            { AT_HWCAP2, getauxval(AT_HWCAP2) },
            { AT_CLKTCK, getauxval(AT_CLKTCK) },
            { AT_PLATFORM, getauxval(AT_PLATFORM) },
            { AT_SYSINFO_EHDR, getauxval(AT_SYSINFO_EHDR) },
            { AT_RANDOM, random_addr },
            { AT_EXECFN, (uintptr_t)argv[1] },
            { AT_NULL, 0 }
        };

        size_t aux_count = sizeof(auxv) / sizeof(auxv[0]);
        const size_t fixed_words = 4 + 2 * aux_count;

        size_t env_count = 0;
        while (environ[env_count] != NULL) {
            env_count++;
        }

        if (env_count >
            (stack_size - stack_alignment) / sizeof(uintptr_t) - fixed_words) {
            fprintf(stderr, "Initial stack entries exceed stack size\n");
            return 1;
        }

        void *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (stack == MAP_FAILED) {
            perror("mmap stack");
            return 1;
        }

        size_t word_count = env_count + fixed_words;
        uintptr_t sp_address = (uintptr_t)stack + stack_size
                            - word_count * sizeof(uintptr_t);
        sp_address -= sp_address % stack_alignment;

        uintptr_t *initial_sp = (uintptr_t *)sp_address;
        size_t slot = 0;

        initial_sp[slot++] = 1;
        initial_sp[slot++] = (uintptr_t)argv[1];
        initial_sp[slot++] = 0;

        for (size_t i = 0; i < env_count; i++) {
            initial_sp[slot++] = (uintptr_t)environ[i];
        }

        initial_sp[slot++] = 0;
        size_t aux_start = slot;

        for (size_t i = 0; i < aux_count; i++) {
            initial_sp[slot++] = auxv[i][0];
            initial_sp[slot++] = auxv[i][1];
        }

        printf("Stack AT_PHDR: 0x%llx\n",
            (unsigned long long)initial_sp[aux_start + 1]);

        printf("Initial stack: sp=%p argc=%llu argv[0]=%s\n",
            (void *)initial_sp,
            (unsigned long long)initial_sp[0],
            (const char *)initial_sp[1]);
        printf("Stack alignment remainder: %llu\n",
            (unsigned long long)(sp_address % stack_alignment));
    }

    // return success
    return 0;
}