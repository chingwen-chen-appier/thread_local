# Background: TLS

`thread_local` means each thread has its own instance of a variable.

```cpp
thread_local int counter = 0;
```

For example:

```text
Thread A          Thread B          Thread C
counter = 1       counter = 5       counter = 0
```

The per-thread memory used to store these variables is called **Thread Local Storage (TLS)**.

# TLS on Linux

On Linux, TLS is managed by the dynamic loader (ld.so) and glibc. Depending on **when a shared library becomes known to the runtime**, its TLS is managed as either **Static TLS** or **Dynamic TLS**.

## Static TLS

### What is Static TLS
Static TLS is used for shared libraries that are known to the dynamic loader **at process startup**.

At process startup, the dynamic loader (`ld.so`) loads the main executable and all shared libraries listed in the executable's `DT_NEEDED` entries (an entry in the ELF file that tells the dynamic loader which shared libraries must be loaded before the program starts).

If an object contains `thread_local` variables, its ELF file includes a `PT_TLS` program header describing its TLS segment.

For example, this project's `libfoo.so` defines a single 4 MB `thread_local` object, and its `PT_TLS` header in ELF file looks like this:

```text
Program Headers:                     (readelf -lW libfoo.so, columns abridged)

  Type           Offset   VirtAddr           FileSiz  MemSiz   Flg Align
  ...
  TLS            0x00fd90 0x000000000001fd90 0x000000 0x400001 R   0x8
```

Two things to read off it:

- `MemSiz` is `0x400001` — the per-thread footprint. That is the 4 MB object plus one byte: a `__tls_guard` flag the compiler adds to record whether the constructor has already run for this thread.
- `FileSiz` is `0x000000`. The object has no initialized data yet.

By scanning the PT_TLS program headers of all objects loaded at startup, the dynamic loader records the size, alignment, and initial contents of each object's TLS segment. These TLS segments are allocated as part of the `Static TLS` block for each thread.

### When is Static TLS allocated

After the process startup, when `pthread_create()` creates a new thread, glibc allocates the thread's runtime data structures, including a static TLS block large enough to hold the TLS segments of all startup-loaded objects:

```text
Thread
├── Stack
├── TCB
│    └── DTV pointer
├── Static TLS block
│     ├── executable TLS
│     └── libfoo TLS
└── DTV
```

Each thread receives its own copy of the static TLS block before it begins executing, so `thread_local` variables can be accessed immediately.

---

## Dynamic TLS

### What is Dynamic TLS

Dynamic TLS is used for shared libraries loaded **after process startup**, typically via `dlopen()`.

Unlike Static TLS, Dynamic TLS is **not** part of the thread's static TLS block. Instead, each thread maintains a **Dynamic Thread Vector (DTV)**, which stores pointers to dynamically allocated per-thread TLS blocks. When a thread first accesses a `thread_local` variable from a dynamically loaded library, the runtime allocates a TLS block for that thread on the heap, stores its address in the DTV, and reuses it for subsequent accesses.

### When is Dynamic TLS allocated

Suppose the program starts with only the main executable:

```text
Process startup

main executable
```

A thread is created:

```text
Thread
├── Stack
├── TCB
│    └── DTV pointer
└── DTV
```

Later, the program loads `libfoo.so`:

```cpp
void* handle = dlopen("libfoo.so", RTLD_NOW);
```

During `dlopen()`, the dynamic loader reads `libfoo.so`'s `PT_TLS` program header and registers it as a new TLS module. However, it does **not** immediately allocate TLS storage for every existing thread.

Instead, TLS for `libfoo.so` is allocated when a thread first accesses one of its `thread_local` variables:

```text
Thread accesses libfoo's thread_local variable
        │
        ▼
Does this thread already have TLS for libfoo?
        ├── Yes ──► Return existing address
        └── No
              │
              ▼
      Allocate libfoo TLS block
              │
              ▼
           Update DTV
              │
              ▼
        Return address
```

This lazy allocation avoids allocating TLS memory for every existing thread when `dlopen()` is called. Only threads that actually access `libfoo.so`'s `thread_local` variables receive a dynamic TLS block.

---

# TLS on Mac

macOS uses a different TLS design from Linux/glibc. It does not have a
static/dynamic TLS split — **all `thread_local` variables are allocated
on first access, regardless of whether the library is linked at startup or
loaded later through `dlopen()`.**

Each `thread_local` variable has a `tlv_descriptor` in `__DATA,__thread_vars`,
while its per-thread storage is described by `__DATA,__thread_bss`:

```c
struct tlv_descriptor
{
    void* (*thunk)(struct tlv_descriptor*);   // dyld sets this to tlv_get_addr
    unsigned long key;                        // pthread key
    unsigned long offset;                     // offset within the block
};
```

Those two sections in `libfoo.dylib`:

```text
(otool -l libfoo.dylib — abridged; flags shown by name)

  sectname __thread_vars    size 0x30       S_THREAD_LOCAL_VARIABLES
  sectname __thread_bss     size 0x400001   S_THREAD_LOCAL_ZEROFILL
                            align 2^0 (1)   offset 0  (past end of file)
```

This is the same information the ELF `PT_TLS` header gave:

| | ELF | Mach-O |
|---|---|---|
| per-thread size | `MemSiz 0x400001` | `size 0x400001` |
| file space used | `FileSiz 0x000000` | `offset 0`, zerofill |

Every access goes through the descriptor's thunk, which dyld points at
`tlv_get_addr` when the image is loaded:

```text
Thread accesses libfoo's thread_local variable
        │
        ▼
        tlv_descriptor
        │
        ▼
        tlv_get_addr()
        │
        ├── already has TLS block?
        │       │
        │       ├── yes → return address
        │       └── no
        │              │
        │              ▼
        │       allocate per-thread TLS block
        │       store under pthread key
        │
        ▼
        return block + offset
```

---

# Test

This experiment defines a **4 MB `thread_local` object** in `libfoo.so` and loads the library in two different ways:

1. Linked at program startup (`DT_NEEDED`) → **Static TLS**
2. Loaded with `dlopen()` at runtime → **Dynamic TLS**

The experiment measures:

- when the TLS memory becomes resident (VmRSS)
- when the `thread_local` constructor runs

`foo()` writes to every page of the object rather than only reading it. glibc
zero-fills the TLS block as it allocates, so on Linux those pages are already
resident and the write changes nothing. macOS only reserves address space and
faults pages in on demand, so without the write the 4 MB never appears in VmRSS
at all.

```
memory.h           printMemory() prints VmRSS (/proc on Linux, Mach task info on macOS)
libfoo.cpp         thread_local LargeTLS (4 MB, ctor/dtor); foo() writes every page of it
main_dynamic.cpp   libfoo is a load-time dependency (Static TLS on Linux)
main_dlopen.cpp    libfoo is dlopen()ed in main(), before the thread starts (Dynamic TLS on Linux)
run.sh             build + run inside a Linux container
run_mac.sh         build + run natively on macOS
```

## Test on Linux

The tests run inside a container, since the static/dynamic TLS split is a glibc
mechanism:

```bash
./run.sh          # build and run both tests
./run.sh Release  # same, optimized
./run.sh shell    # interactive container (pmap / gdb available)
```

## Test on Mac

The same two programs build and run natively:

```bash
./run_mac.sh          # build and run both tests
./run_mac.sh Release  # same, optimized
```

---

# Test Results (aarch64 / glibc 2.39)

`libfoo.so` defines a single 4 MB `thread_local` object.

## A. Startup-loaded library (`DT_NEEDED` → Static TLS)

| Event | VmRSS | ΔVmRSS | What happens |
|---|---:|---:|---|
| program start | 7,080 kB | — | Baseline already includes the 4 MB TLS block. The dynamic loader allocated the main thread's static TLS before `main()`. |
| thread start | 11,176 kB | +4,096 | `pthread_create()` allocates and zero-initializes the worker thread's static TLS before the thread begins execution. |
| TLS constructor | — | — | Constructor runs when the variable is first referenced. No measurement — this row marks output ordering only. |
| after `foo()` | 11,176 kB | +0 | Writing all 4 MB adds no pages, because the TLS memory is already resident. |
| TLS destructor | — | — | Destructor runs when the thread exits. Ordering only. |
| program end | 11,176 kB | +0 | Memory remains cached by glibc's thread stack cache. |

## B. `dlopen()` (`Dynamic TLS`)

`dlopen()` happens in `main()`, **before** the worker thread is created, so
`thread start` shows whether thread creation allocates TLS for a library the
loader already knows about.

| Event | VmRSS | ΔVmRSS | What happens |
|---|---:|---:|---|
| program start | 2,956 kB | — | `libfoo.so` is not loaded yet. |
| after `dlopen()` | 2,956 kB | +0 | `libfoo.so` is loaded and registered as a TLS module, but no per-thread TLS memory is allocated. |
| thread start | 2,956 kB | +0 | **The key measurement.** The module was already registered before this thread existed, yet `pthread_create()` still allocates nothing for it — a dlopen'd module never joins the static TLS block. |
| TLS constructor | — | — | First call to `__tls_get_addr()` allocates and zero-initializes the 4 MB TLS block, then runs the constructor. Ordering only. |
| after `foo()` | 7,068 kB | +4,112 | TLS allocation occurred during the first access. |
| TLS destructor | — | — | Runs when the thread exits, which is now before `dlclose()`. Ordering only. |
| after `dlclose()` | 7,068 kB | +0 | The thread has already exited, so the destructor registered through `__cxa_thread_atexit_impl()` has run and released its reference — `libfoo.so` really is unloaded here (confirmed in `/proc/self/maps`). VmRSS does not drop because glibc's allocator retains the freed pages. |
| program end | 7,068 kB | +0 | — |

---

# Test Results (macOS 15.6 / arm64 / dyld)

There is no static TLS block here (see **TLS on Mac** above), so the question is
not "static or dynamic" but simply: **does load-time linking allocate any
earlier than `dlopen()`?**

## A. Load-time dependency

| Event | VmRSS | ΔVmRSS | What happens |
|---|---:|---:|---|
| program start | 1,216 kB | — | Baseline does **not** include the TLS block, unlike glibc. |
| thread start | 1,248 kB | +32 | Thread creation reserves no TLS. |
| TLS constructor | — | — | Runs on first access, inside `foo()`. Ordering only. |
| after `foo()` | 5,424 kB | +4,176 | Block allocated on first access, then made resident by the write. |
| TLS destructor | — | — | Registered with `__tlv_atexit()`; runs when the thread exits. Ordering only. |
| program end | 5,408 kB | +0 | — |

## B. `dlopen()`

As on Linux, `dlopen()` runs in `main()` before the thread is created.

| Event | VmRSS | ΔVmRSS | What happens |
|---|---:|---:|---|
| program start | 1,168 kB | — | `libfoo.dylib` is not loaded yet. |
| after `dlopen()` | 1,248 kB | +80 | Image mapped. The 80 kB is the mapping itself, not TLS. |
| thread start | 1,264 kB | +16 | Nothing allocated for the already-registered image, same as Linux. |
| TLS constructor | — | — | Runs on first access, inside `foo()`. Ordering only. |
| after `foo()` | 5,408 kB | +4,144 | Same allocation point as case A. |
| TLS destructor | — | — | Registered with `__tlv_atexit()`; runs at thread exit. Ordering only. |
| after `dlclose()` | 5,392 kB | +0 | Returns 0, but the image stays loaded — unlike Linux. dyld refuses to unload any image containing thread-local variables; a control dylib with no `thread_local` unloads normally at `dlclose()`. |
| program end | 5,392 kB | +0 | — |

**Both cases allocate at the same point: first access.** Load-time linking buys
nothing earlier. macOS case A behaves like Linux case B.

---

# Conclusions
For linux:
- **Static TLS** is allocated when each thread is created.
- **Dynamic TLS** is allocated when the first time each thread accesses a `thread_local` variable from a library loaded with `dlopen()`.

For macOS:
Both load-time linking and `dlopen()` allocate TLS on first access.



---

# Cross-checking with ELF

```bash
readelf -d dynamic_app | grep NEEDED   # libfoo.so appears in DT_NEEDED
readelf -d dlopen_app  | grep NEEDED   # libfoo.so does not appear
readelf -lW libfoo.so  | grep TLS      # PT_TLS segment
```

# Cross-checking with Mach-O

```bash
otool -L dynamic_app | grep libfoo         # @rpath/libfoo.dylib is a dependency
otool -L dlopen_app  | grep libfoo         # no match
otool -l libfoo.dylib | grep -A4 __thread  # __thread_bss 0x400001, __thread_vars 0x30
nm -mu libfoo.dylib | grep -i tlv          # __tlv_bootstrap, __tlv_atexit
```

See **TLS on Mac** above for what those sections contain.
