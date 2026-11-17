# ELF Loader and Paging

This project implements three user-space loaders for static Linux
x86-64 ELF executables:

- apager maps all loadable segments before starting the target.
- dpager initially maps one target page and loads further pages on demand.
- hpager maps code and initialized data at startup, then loads BSS pages
  on demand with sequential page prediction.

The loaders prepare a separate initial stack and transfer control to
the target's ELF entry point.

## Build and run

Requirements: Linux x86-64, GCC, Make, and static C library development
files. GNU Time is used for performance measurements.

From the repository root:

```bash
make -C hw1 clean
make -C hw1
```

Run the memory test with each main loader:

```bash
./hw1/build/apager hw1/build/memory-static
./hw1/build/dpager hw1/build/memory-static
./hw1/build/hpager hw1/build/memory-static
```

Additional hybrid builds use one or three pages per fault:

```bash
./hw1/build/hpager-one hw1/build/memory-static
./hw1/build/hpager-three hw1/build/memory-static
```

Run the dynamic linking experiments directly:

```bash
LD_DEBUG=symbols ./hw1/build/hello-dynamic \
    2> hw1/build/hello-symbols.log

LD_PRELOAD="$PWD/hw1/build/malloc-hook.so" \
    ./hw1/build/preload-test
```

The loaders print target mapping information to standard error.
Generated binaries and logs are stored in hw1/build.

## Dense memory benchmark

The dense workload uses a 64 MiB global buffer. It checks every byte
for zero initialization, writes every byte, and verifies the values.

I ran one warm-up per loader, followed by five measured runs per loader
in interleaved order. Measurements used GNU Time. Program output and
mapping logs were redirected to /dev/null.

The following values are arithmetic means across five runs.
All measured executions returned exit status 0.

| Loader | Elapsed (s) | User (s) | System (s) | Max RSS (KiB) |
|---|---:|---:|---:|---:|
| apager | 0.380 | 0.356 | 0.020 | 67840 |
| dpager | 0.420 | 0.372 | 0.046 | 67456 |
| hpager-one | 0.422 | 0.374 | 0.044 | 67840 |
| hpager | 0.406 | 0.366 | 0.038 | 67840 |
| hpager-three | 0.406 | 0.364 | 0.034 | 67840 |

Raw measurements are stored in benchmark-dense.csv.

Apager had approximately 9.5% lower elapsed time than dpager.
The workload touches every buffer page, so loading pages individually
adds signal-handling and mapping overhead without avoiding much work.

The two-page and three-page hybrid variants both had approximately
3.8% lower mean elapsed time than the one-page hybrid variant.
This agrees with the sequential access pattern, where nearby pages
are likely to be accessed next. These measurements do not show a
mean elapsed-time advantage for three pages over two pages.

Peak resident memory was similar because the workload eventually
accesses the entire buffer.

These measurements used targets compiled with -g and without an
optimization flag. Redirecting logs avoids terminal output, but
does not remove the cost of generating and writing mapping messages.

## Hybrid prediction

Hpager maps file-backed segment pages at startup and loads the
remaining BSS pages on demand.

On a recoverable fault, it loads the required page and predicts the
following page in the same segment. The three-page variant predicts
the following two pages. Already loaded pages are not mapped again,
and prediction stops at the segment boundary.

For the separate 1 MiB sequential memory test, the observed numbers
of successfully handled loader page faults were:

| Pages per fault | Handled faults |
|---|---:|
| 1 | 258 |
| 2 | 130 |
| 3 | 87 |

These are counts from the loader's fault log, not operating-system
minor or major page-fault counters. Fewer handled faults do not
necessarily imply a proportional reduction in execution time.

## Sparse memory test

The test declares a 64 MiB global buffer but accesses only its first
and last bytes. All three execution modes passed with exit status 0.

| Execution | VmSize (KiB) | VmRSS (KiB) |
|---|---:|---:|
| Direct | 66656 | 640 |
| apager | 70308 | 2304 |
| dpager | 4240 | 1920 |

Measurements were read from /proc/self/status after accessing the buffer.
They describe the whole process at that point, including the loader.

Dpager used a much smaller mapped virtual address space. The resident
memory difference was smaller because mapping memory does not necessarily
make all of its pages resident. VmRSS is an approximate measurement.

These results are from one run and are not peak-memory measurements.

## Dynamic linking: LD_DEBUG

I compiled hello_dynamic.c with GCC's default options and ran it with
LD_DEBUG=symbols.

The log contained 188 symbol lookup attempts for 84 unique symbol names.
These counts describe this execution environment and may differ on
other systems.

For example, puts was searched for in the executable and then in libc.so.6.
Although the source uses printf, GCC converted this particular call to puts.

Dynamic libraries allow programs to share library code in memory and
reduce executable file sizes. Compatible library updates can also be
installed without rebuilding every application.

The trade-offs include runtime symbol resolution overhead, dependency
and version compatibility requirements, and dependence on the installed
shared libraries.

## Dynamic linking: LD_PRELOAD

LD_PRELOAD loads a shared library before the normal dependencies.
It can be used to intercept library functions for debugging,
instrumentation, or testing.

My shared library defines malloc and uses dlsym(RTLD_NEXT, "malloc")
to find the next definition of malloc after this library.

The hook prints the requested size, calls the original allocator,
fills successful allocations with 0xCC, and prints the returned address.
Logging uses write and a stack-based number conversion helper to avoid
using printf inside malloc.

The test allocated 64 bytes and verified that every byte contained 0xCC.
It passed with exit status 0 when run with LD_PRELOAD.

This is a simple single-threaded demonstration. It does not handle
recursive allocation during dlsym initialization or concurrent
initialization from multiple threads.

## Test programs

| Test | Purpose |
|---|---|
| hello-static | Check basic startup, output, and termination |
| memory-static | Check initialized data, zero-initialized BSS, and writable memory |
| memory-large-static | Exercise sequential access to a 64 MiB global buffer |
| sparse-static | Access only the first and last bytes of a 64 MiB global buffer |
| malloc-static | Allocate, write, verify, and free a 1 MiB heap buffer |
| fault-static | Check that a NULL-pointer access still causes SIGSEGV |
| readonly-static | Check that writing to read-only data still causes SIGSEGV |
| preload-test | Verify that the malloc hook fills allocations with 0xCC |

The current hello-static test returns 1 both directly and through
the loader. Its matching exit status is intentional for this comparison;
the other successful functional tests return 0.

## Malloc in the loaded program

The malloc-static test successfully allocated a 1 MiB buffer, wrote
and verified every byte, and freed the allocation when run through
each loader variant.

The target runs in the loader's process, so its allocator uses the
same process's kernel-managed address space. The loaders do not
implement malloc or intercept its system calls.

The C allocator can request memory from the kernel using brk or mmap.
The successful test does not establish which mechanism was used;
I did not trace the allocation system calls.

Memory allocated by the target through these system calls is managed
by the kernel rather than by the loader's PT_LOAD demand-paging logic.

## Final functional checks

After a clean build, I tested memory-static, malloc-static, and
sparse-static with apager, dpager, hpager, hpager-one, and hpager-three.
All 15 combinations completed with exit status 0.

I also tested fault-static and readonly-static with apager, dpager,
and hpager. All six combinations terminated with SIGSEGV (exit status
139 in my shell).

These tests check that the loaders preserve NULL-pointer access errors
and writes to read-only memory for these test programs.

## Linux ELF loading

Reference: upstream Linux v6.8, fs/binfmt_elf.c.
My running Ubuntu kernel is 6.8.0-57-generic.

### Validation before replacing the current program

```text
Read the ELF header from the executable information already prepared.

Reject an invalid ELF signature, unsupported executable type,
incompatible architecture, or a file that cannot be memory-mapped.

Read the executable's program header table.

If PT_INTERP exists:
    Read and validate the interpreter pathname.
    Open the interpreter.
    Read and validate its ELF header and program headers.

Determine the requested stack execution permissions from PT_GNU_STACK.
Process architecture-specific headers and ELF properties.

If any check fails:
    Release temporary resources and return an error.
```

At this stage, validation can still fail before the old program's
memory image is replaced.

### Preparing the new address space and mapping segments

```text
Begin replacing the current executable image.
Configure execution properties and address randomization.
Set up the new program's stack mapping.

For each PT_LOAD program header:
    Derive memory permissions from its ELF flags.

    For ET_EXEC:
        Use the specified virtual addresses.

    For ET_DYN:
        Determine the load bias and required mapping layout.

    Map the segment at load_bias + p_vaddr using elf_load.
    If mapping fails, enter the execution failure path.
```

Exec replaces the program image within the existing process.
Unlike my user-space loader, it does not keep the old loader image
alongside the target program.

Mapping an executable does not require all of its pages to become
resident immediately; the kernel can bring pages into memory on demand.

### Finalizing the program image

```text
Finish processing the loadable segments:
    Locate the program header table in memory.
    Validate segment sizes and address ranges.
    Track code, data, and memory boundaries.

Apply the load bias to the recorded addresses.
Initialize the program break after the loaded image.

If an interpreter exists:
    Load its segments.
    Select the interpreter's entry point as the initial execution address.
Otherwise:
    Select the executable's entry point.

Set up additional architecture-specific mappings, if required.
Build the initial stack tables: argc, argv, envp, and auxv.
Record the memory layout and apply heap randomization if enabled.

Initialize the required user register state.
Finalize execution setup.
Set the user instruction pointer and stack pointer.
Resume in user space at the selected entry point.
```

For dynamically linked programs, execution initially enters the
interpreter, while AT_ENTRY identifies the main executable's entry point.
The kernel does not call the program's main function directly.

### Zero-initialized segment memory

```text
If a segment has file-backed bytes:
    Map those bytes from the executable.

If its memory size exceeds its file size:
    Zero the remaining part of the last file-backed page, if present.
    Create anonymous zero-initialized mappings for the remaining pages.
```

This explains why copying or mapping only p_filesz bytes is insufficient:
the full p_memsz range must be available, including zero-initialized data.

### Initial stack preparation

create_elf_tables aligns the stack, copies platform information and
16 random bytes into user memory, and constructs auxiliary-vector entries.
These include the program header address, entry size, entry count,
page size, and interpreter base address.

### Completing the initial stack

```text
Add auxiliary entries such as AT_ENTRY, user/group IDs,
AT_SECURE, AT_RANDOM, and AT_EXECFN.
Terminate the auxiliary vector with AT_NULL.

Calculate the space required for the stack tables.
Align the initial stack pointer and ensure the stack range is available.

Write argc.
Write pointers to the existing argument strings, followed by NULL.
Write pointers to the existing environment strings, followed by NULL.
Copy the auxiliary-vector pairs after the environment pointer list.

Check user-memory writes and report failures.
```

The initial stack pointer identifies argc, not a normal C function's
return address. Execution begins at the selected ELF entry point;
the startup code prepares the runtime before calling main.

Sources:
- https://github.com/torvalds/linux/blob/v6.8/fs/binfmt_elf.c
- https://github.com/torvalds/linux/blob/v6.8/fs/exec.c

## Supported targets and limitations

The loaders support statically linked, non-PIE ELF64 executables for
Linux x86-64. Targets use ET_EXEC and little-endian encoding.
Dynamically linked targets are rejected.

The loader itself is built as PIE to reduce address conflicts with
the target. Target mappings are checked for conflicts with existing
mappings. Loadable segments that share a memory page are not supported.

Demand and hybrid paging targets are built with -Wl,-z,norelro.
With partially unmapped segments, the static C runtime's RELRO
protection setup can fail because it attempts to protect a range
containing unmapped pages.

The demand pager handles user-space accesses through SIGSEGV.
A system call that accesses an untouched, unmapped target buffer
may instead return EFAULT; such accesses are not fully supported.

The initial target stack contains one argument: the target pathname.
Additional command-line arguments are not supported.

The malloc preload hook is a single-threaded demonstration.
It does not handle recursive allocation during dlsym initialization
or concurrent initialization.

Performance measurements include loader overhead and mapping-log
generation, even when output is redirected. They describe the tested
workloads and environment, rather than all possible programs.

## Time spent

I spent approximately ___ hours on this lab, including studying,
implementation, debugging, testing, and writing the report.