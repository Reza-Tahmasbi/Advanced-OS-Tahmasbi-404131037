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
#include <signal.h>
#include <errno.h>

#ifndef HYBRID_PAGES_PER_FAULT
#define HYBRID_PAGES_PER_FAULT 2
#endif

static Elf64_Phdr *load_segments;
static size_t load_count = 0;

#ifdef DEMAND_PAGING
static int target_fd = -1;
static Elf64_Xword pager_page_size = 0;
static unsigned char **loaded_pages;
#endif

static const Elf64_Phdr *find_load_segment(uintptr_t address)
{
    for (size_t i=0; i<load_count; i++){
        const Elf64_Phdr *segment = &load_segments[i];

        if (address >= segment->p_vaddr &&
            address - segment->p_vaddr < segment->p_memsz) {
            return segment;
        }
    }

    return NULL;
}

#ifdef DEMAND_PAGING
static void *load_target_page(int fd, const Elf64_Phdr *segment,
                              Elf64_Addr page_start, Elf64_Xword page_size)
{
    if (page_size == 0 || page_start % page_size != 0 ||
        page_start > UINT64_MAX - page_size) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    Elf64_Addr page_end = page_start + page_size;
    Elf64_Addr segment_end = segment->p_vaddr + segment->p_memsz;

    if (page_start >= segment_end || page_end <= segment->p_vaddr) {
        errno = EINVAL;
        return MAP_FAILED;
    }

    size_t segment_index = (size_t)(segment - load_segments);
    Elf64_Addr segment_page =
        segment->p_vaddr - segment->p_vaddr % page_size;
    Elf64_Xword page_index = (page_start - segment_page) / page_size;
    unsigned char mask = (unsigned char)(1u << (page_index % 8));

    if (loaded_pages[segment_index][page_index / 8] & mask) {
        errno = EEXIST;
        return MAP_FAILED;
    }

    int prot = 0;
    if (segment->p_flags & PF_R) {
        prot |= PROT_READ;
    }
    if (segment->p_flags & PF_W) {
        prot |= PROT_WRITE;
    }
    if (segment->p_flags & PF_X) {
        prot |= PROT_EXEC;
    }

    Elf64_Addr file_end = segment->p_vaddr + segment->p_filesz;
    int flags = MAP_PRIVATE | MAP_FIXED_NOREPLACE;
    int map_fd = -1;
    off_t offset = 0;
    Elf64_Xword zero_length = 0;

    if (segment->p_filesz > 0 && page_start < file_end) {
        Elf64_Off file_page =
            segment->p_offset - segment->p_offset % page_size;

        offset = (off_t)(file_page + (page_start - segment_page));
        map_fd = fd;

        if (file_end < page_end && file_end < segment_end) {
            Elf64_Addr zero_end = page_end;
            if (zero_end > segment_end) {
                zero_end = segment_end;
            }
            zero_length = zero_end - file_end;
        }
    } else {
        flags |= MAP_ANONYMOUS;
    }

    int map_prot = prot;
    if (zero_length > 0) {
        map_prot = PROT_READ | PROT_WRITE;
    }

    void *requested = (void *)(uintptr_t)page_start;
    void *mapped = mmap(requested, (size_t)page_size,
                        map_prot, flags, map_fd, offset);

    if (mapped == MAP_FAILED) {
        return MAP_FAILED;
    }

    if (mapped != requested) {
        munmap(mapped, (size_t)page_size);
        errno = EINVAL;
        return MAP_FAILED;
    }

    if (zero_length > 0) {
        memset((void *)(uintptr_t)file_end, 0, (size_t)zero_length);
    }

    if (map_prot != prot &&
        mprotect(mapped, (size_t)page_size, prot) == -1) {
        int saved_errno = errno;
        munmap(mapped, (size_t)page_size);
        errno = saved_errno;
        return MAP_FAILED;
    }

    loaded_pages[segment_index][page_index / 8] |= mask;
    return mapped;
}
#endif

#ifdef DEMAND_PAGING
static size_t append_hex(char *buffer, size_t length, Elf64_Xword value)
{
    const char digits[] = "0123456789abcdef";

    buffer[length++] = '0';
    buffer[length++] = 'x';
    for (int shift=60; shift>=0; shift-=4){
        buffer[length++] = digits[(value >> shift) & 0xf];
    }

    return length;
}

static void log_page_mapping(const Elf64_Phdr *segment, Elf64_Addr page)
{
    char message[128];
    const char prefix[] = "Mapped address=";
    const char offset_label[] = " offset=";
    const char size_label[] = " length=";
    size_t length = sizeof(prefix) - 1;

    memcpy(message, prefix, length);
    length = append_hex(message, length, page);

    memcpy(message + length, offset_label, sizeof(offset_label) - 1);
    length += sizeof(offset_label) - 1;

    Elf64_Addr file_end = segment->p_vaddr + segment->p_filesz;
    if (segment->p_filesz > 0 && page < file_end) {
        Elf64_Addr segment_page =
            segment->p_vaddr - segment->p_vaddr % pager_page_size;
        Elf64_Off file_page =
            segment->p_offset - segment->p_offset % pager_page_size;
        length = append_hex(message, length,
                            file_page + (page - segment_page));
    } else {
        message[length++] = 'N';
        message[length++] = '/';
        message[length++] = 'A';
    }

    memcpy(message + length, size_label, sizeof(size_label) - 1);
    length += sizeof(size_label) - 1;
    length = append_hex(message, length, pager_page_size);
    message[length++] = '\n';

    (void)write(STDERR_FILENO, message, length);
}

static int prepare_demand_pager(void)
{
    size_t table_size = load_count * sizeof(*loaded_pages);

    loaded_pages = mmap(NULL, table_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (loaded_pages == MAP_FAILED) {
        return -1;
    }

    Elf64_Addr previous_end = 0;

    for (size_t i=0; i<load_count; i++){
        const Elf64_Phdr *segment = &load_segments[i];
        Elf64_Addr start =
            segment->p_vaddr - segment->p_vaddr % pager_page_size;
        Elf64_Addr end = segment->p_vaddr + segment->p_memsz;
        end += (pager_page_size - end % pager_page_size) % pager_page_size;

        if (i > 0 && start < previous_end) {
            fprintf(stderr, "Overlapping or unordered LOAD pages are not supported\n");
            errno = EINVAL;
            return -1;
        }
        previous_end = end;

        Elf64_Xword page_count = (end - start) / pager_page_size;
        size_t bitmap_size =
            (size_t)(page_count / 8 + (page_count % 8 != 0));

        loaded_pages[i] = mmap(NULL, bitmap_size,
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (loaded_pages[i] == MAP_FAILED) {
            return -1;
        }
    }

    FILE *maps = fopen("/proc/self/maps", "r");
    if (maps == NULL) {
        return -1;
    }

    char line[1024];
    while (fgets(line, sizeof(line), maps) != NULL) {
        unsigned long mapped_start, mapped_end;
        if (sscanf(line, "%lx-%lx", &mapped_start, &mapped_end) != 2) {
            continue;
        }

        for (size_t i=0; i<load_count; i++){
            const Elf64_Phdr *segment = &load_segments[i];
            Elf64_Addr start =
                segment->p_vaddr - segment->p_vaddr % pager_page_size;
            Elf64_Addr end = segment->p_vaddr + segment->p_memsz;
            end += (pager_page_size - end % pager_page_size) % pager_page_size;

            if (start < (Elf64_Addr)mapped_end &&
                (Elf64_Addr)mapped_start < end) {
                fprintf(stderr, "Target LOAD overlaps an existing mapping\n");
                fclose(maps);
                errno = EEXIST;
                return -1;
            }
        }
    }

    if (ferror(maps)) {
        fclose(maps);
        errno = EIO;
        return -1;
    }

    return fclose(maps);
}

static void segv_handler(int sig, siginfo_t *info, void *context)
{
    (void)context;

    int saved_errno = errno;
    uintptr_t address = (uintptr_t)info->si_addr;
    const Elf64_Phdr *segment = find_load_segment(address);

    if (segment != NULL && info->si_code == SEGV_MAPERR) {
        Elf64_Addr page = address - address % pager_page_size;
        void *mapped =
            load_target_page(target_fd, segment, page, pager_page_size);

        if (mapped != MAP_FAILED) {
            const char message[] = "Handled page fault\n";
            (void)write(STDERR_FILENO, message, sizeof(message) - 1);

            log_page_mapping(segment, page);

#ifdef HYBRID_PAGING
            Elf64_Addr segment_end =
                segment->p_vaddr + segment->p_memsz;
            Elf64_Addr predicted_page = page;

            for (unsigned int i=1; i<HYBRID_PAGES_PER_FAULT; i++){
                predicted_page += pager_page_size;

                if (predicted_page >= segment_end) {
                    break;
                }

                if (load_target_page(target_fd, segment, predicted_page,
                                     pager_page_size) != MAP_FAILED) {
                    log_page_mapping(segment, predicted_page);
                }
            }
#endif

            errno = saved_errno;
            return;
        }
    }

    if (segment != NULL) {
        const char message[] = "Unrecoverable SIGSEGV inside a target LOAD segment\n";
        (void)write(STDERR_FILENO, message, sizeof(message) - 1);
    } else {
        const char message[] = "SIGSEGV outside target LOAD segments\n";
        (void)write(STDERR_FILENO, message, sizeof(message) - 1);
    }

    struct sigaction action = {0};
    action.sa_handler = SIG_DFL;

    if (sigemptyset(&action.sa_mask) == -1 ||
        sigaction(sig, &action, NULL) == -1 ||
        raise(sig) != 0) {
        _exit(128 + sig);
    }
}
#endif

__attribute__((noreturn))
void jump_to_target(uintptr_t entry, uintptr_t *initial_sp);

__asm__(
    ".pushsection .text\n"
    ".globl jump_to_target\n"
    ".type jump_to_target, @function\n"
    "jump_to_target:\n"
    "mov %rsi, %rsp\n"
    "mov %rdi, -8(%rsp)\n"
    "xor %eax, %eax\n"
    "xor %ebx, %ebx\n"
    "xor %ecx, %ecx\n"
    "xor %edx, %edx\n"
    "xor %esi, %esi\n"
    "xor %edi, %edi\n"
    "xor %ebp, %ebp\n"
    "xor %r8d, %r8d\n"
    "xor %r9d, %r9d\n"
    "xor %r10d, %r10d\n"
    "xor %r11d, %r11d\n"
    "xor %r12d, %r12d\n"
    "xor %r13d, %r13d\n"
    "xor %r14d, %r14d\n"
    "xor %r15d, %r15d\n"
    "cld\n"
    "jmp *-8(%rsp)\n"
    ".size jump_to_target, .-jump_to_target\n"
    ".popsection\n"
);

// a simple file path reader
int main (int argc, char *argv[])
{
    if (argc != 2){
        fprintf(stderr, "Usage: %s <program>\n", argv[0]);
        return 1;
    }

    #if defined(HYBRID_PAGING)
        fprintf(stderr, "Build mode: hpager\n");
    #elif defined(DEMAND_PAGING)
        fprintf(stderr, "Build mode: dpager\n");
    #else
        fprintf(stderr, "Build mode: apager\n");
    #endif

    // open for read_only, fd is the file descriptor
    int fd = open(argv[1], O_RDONLY);

    // if file opening fails
    if (fd == -1){
        perror("open");
        return 1;
    }
    #ifdef DEMAND_PAGING
        target_fd = fd;
    #endif
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

    if (header.e_type != ET_EXEC) {
        fprintf(stderr, "Only non-PIE ET_EXEC targets are supported\n");
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

    #ifdef DEMAND_PAGING
    pager_page_size = page_size;
    #endif

    Elf64_Addr target_phdr = 0;
    int found_phdr = 0;
    int entry_valid = 0;
    Elf64_Xword phdr_size =
    (Elf64_Xword)header.e_phnum * header.e_phentsize;

    if (header.e_phnum == 0 || header.e_phnum == PN_XNUM) {
        fprintf(stderr, "Missing or unsupported program header count\n");
        close(fd);
        return 1;
    }

    size_t segment_table_size =
        (size_t)header.e_phnum * sizeof(*load_segments);

    load_segments = mmap(NULL, segment_table_size,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (load_segments == MAP_FAILED) {
        perror("mmap segment table");
        close(fd);
        return 1;
    }

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

        if (header.e_type == ET_EXEC && segment.p_type == PT_INTERP) {
            fprintf(stderr, "Dynamically linked targets are not supported\n");
            close(fd);
            return 1;
        }

#ifdef DEMAND_PAGING
        if (segment.p_type == PT_GNU_RELRO && segment.p_memsz > 0) {
            fprintf(stderr, "Build demand-paging targets with -Wl,-z,norelro\n");
            close(fd);
            return 1;
        }
#endif

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

            load_segments[load_count] = segment;
            load_count++;

            if (header.e_type == ET_EXEC) {
                #ifdef DEMAND_PAGING
                Elf64_Addr file_end = segment.p_vaddr + segment.p_filesz;
                #else
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
#endif
                if ((segment.p_flags & PF_X) &&
                    header.e_entry >= segment.p_vaddr &&
                    header.e_entry < file_end) {
                    entry_valid = 1;
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
    fprintf(stderr, "Stored LOAD segments: %zu\n", load_count);

    // if the file is not closed yet, close it(on success path)
#ifndef DEMAND_PAGING
    // if the file is not closed yet, close it(on success path)
    if (close(fd) == -1) {
        perror("close");
        return 1;
    }
#endif

    if (header.e_type == ET_EXEC) {
        extern char **environ;
        const size_t stack_size = 1024 * 1024;
        const size_t stack_alignment = 16;
        const Elf64_Phdr *entry_segment =
    find_load_segment((uintptr_t)header.e_entry);

    if (entry_segment == NULL) {
        fprintf(stderr, "Entry address does not belong to a LOAD segment\n");
        return 1;
    }

    fprintf(stderr, "Entry segment: start=0x%llx size=0x%llx\n",
        (unsigned long long)entry_segment->p_vaddr,
        (unsigned long long)entry_segment->p_memsz);
        if (!entry_valid) {
            fprintf(stderr, "Entry point is outside executable file-backed segments\n");
            return 1;
        }
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
        if (fflush(NULL) == EOF) {
            perror("fflush");
            return 1;
        }

        #ifdef DEMAND_PAGING
            stack_t signal_stack = {0};
            signal_stack.ss_size = (size_t)SIGSTKSZ;
            signal_stack.ss_sp =
                mmap(NULL, signal_stack.ss_size,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

            if (signal_stack.ss_sp == MAP_FAILED) {
                perror("mmap signal stack");
                return 1;
            }
            if (sigaltstack(&signal_stack, NULL) == -1) {
                perror("sigaltstack");
                return 1;
            }

            if (prepare_demand_pager() == -1) {
                perror("prepare demand pager");
                return 1;
            }

            #ifdef HYBRID_PAGING
            size_t initial_pages = 0;

            for (size_t i=0; i<load_count; i++){
                const Elf64_Phdr *segment = &load_segments[i];

                if (segment->p_filesz == 0) {
                    continue;
                }

                Elf64_Addr first_page =
                    segment->p_vaddr - segment->p_vaddr % pager_page_size;
                Elf64_Addr file_end =
                    segment->p_vaddr + segment->p_filesz;

                for (Elf64_Addr page=first_page;
                     page<file_end; page+=pager_page_size){
                    if (load_target_page(target_fd, segment, page,
                                         pager_page_size) == MAP_FAILED) {
                        perror("load initial hybrid page");
                        return 1;
                    }

                    log_page_mapping(segment, page);
                    initial_pages++;
                }
            }

            fprintf(stderr, "Initial target pages: %zu\n", initial_pages);
#else
            Elf64_Addr entry_page =
                header.e_entry - header.e_entry % pager_page_size;

            if (load_target_page(target_fd, entry_segment,
                                 entry_page, pager_page_size) == MAP_FAILED) {
                perror("load entry page");
                return 1;
            }

            log_page_mapping(entry_segment, entry_page);
            fprintf(stderr, "Initial target pages: 1\n");
#endif

            struct sigaction action = {0};
            action.sa_sigaction = segv_handler;
            action.sa_flags = SA_SIGINFO | SA_ONSTACK;

            if (sigemptyset(&action.sa_mask) == -1 ||
                sigaction(SIGSEGV, &action, NULL) == -1) {
                perror("install SIGSEGV handler");
                return 1;
            }
        #endif

        jump_to_target((uintptr_t)header.e_entry, initial_sp);
    }

    // return success
    return 0;
}