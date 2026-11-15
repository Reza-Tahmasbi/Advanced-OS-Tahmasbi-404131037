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