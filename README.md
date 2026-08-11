# TLS in shared libraries: startup-loaded vs `dlopen()`

A `thread_local` variable defined in a shared library needs per-thread memory.
**When is that memory actually allocated?**

This README compares two ways of loading the same library:

1. loaded by the dynamic loader at program startup
2. loaded at runtime with `dlopen()`

and how the answer differs between **Linux** and **macOS**.

---

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

---

# Background: ELF and Mach-O

A program does not contain the shared libraries it depends on. At startup, the dynamic loader loads the required libraries into memory and resolves their references. On `Linux`, this is `ld.so`; on `macOS`, `dyld`.

Executables and shared libraries use platform-specific binary formats: ELF on Linux and Mach-O on macOS. These formats provide the loader with information about required libraries, code and data locations, and thread-local storage (TLS) requirements.

| | Linux | macOS |
|---|---|---|
| dynamic loader | `ld.so` | `dyld` |
| file format | ELF | Mach-O |
| shared library | `libfoo.so` | `libfoo.dylib` |
| inspect it with | `readelf` | `otool` |

For example in Linux, the dynamic loader (`ld.so`) loads the main executable and all shared libraries listed in the executable's `DT_NEEDED` entries (an entry in the ELF file that tells the dynamic loader which shared libraries must be loaded before the program starts).

If an object contains `thread_local` variables, its ELF file includes a `PT_TLS` program header describing its TLS segment.

```text
(readelf -lW libfoo.so)

Program Headers:

  Type           Offset   VirtAddr           FileSiz  MemSiz   Flg Align
  ...
  TLS            0x00fd90 0x000000000001fd90 0x000000 0x400001 R   0x8
```

Two things to read off it:

- `MemSiz` is `0x400001` — the per-thread footprint. That is the 4 MB object plus one byte: a `__tls_guard` flag the compiler adds to record whether the constructor has already run for this thread.
- `FileSiz` is `0x000000`. The object has no initialized data yet.

By scanning the `PT_TLS` program headers of all objects, the dynamic loader records each object's TLS size, alignment, and initial contents, which it uses to construct the initial TLS layout for each thread.

---

# TLS on Linux

On Linux, TLS is managed by glibc — its dynamic loader (`ld.so`) and its threading runtime. Depending on **when a shared library becomes known to the runtime**, its TLS is managed as either **Static TLS** or **Dynamic TLS**.

## Static TLS

### What is Static TLS

Static TLS is used for shared libraries that are known to the dynamic loader **at process startup**.

### When is Static TLS allocated

After process startup, when `pthread_create()` creates a new thread, glibc allocates the thread's runtime data structures, including a static TLS block large enough to hold the TLS segments of all startup-loaded objects:

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

Suppose the program starts with no shared libraries loaded.

A thread is created:

```text
Thread
├── Stack
├── TCB
│    └── DTV pointer
├── Static TLS block
│     ├── executable TLS
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

# TLS on macOS

## What is TLS on macOS

macOS uses a different TLS design from Linux/glibc. It does not have a static/dynamic TLS split — **all `thread_local` variables are allocated on first access, regardless of whether the library is loaded at startup or later through `dlopen()`.**

A Mach-O file is divided into named sections, each holding one kind of thing. For `thread_local` variables the compiler fills in two of them:

- `__DATA,__thread_vars` — **how to find the variable at runtime.** It holds one 24-byte `tlv_descriptor` per variable.
- `__DATA,__thread_bss` — **how much memory each thread needs.**

For example, this project's `libfoo.dylib` has these two sections:

```text
(otool -l libfoo.dylib)

  sectname __thread_vars    size 0x30       S_THREAD_LOCAL_VARIABLES
  sectname __thread_bss     size 0x400001   S_THREAD_LOCAL_ZEROFILL
                            align 2^0 (1)   offset 0  (past end of file)
```
```c
struct tlv_descriptor
{
    void* (*thunk)(struct tlv_descriptor*);   // dyld sets this to tlv_get_addr()
    unsigned long key;                        // pthread key
    unsigned long offset;                     // offset within the block
};
```

The descriptor holds no data of its own — only the information needed to locate the variable when a thread asks for it.

This is the same information the ELF `PT_TLS` header gave:

| | ELF | Mach-O |
|---|---|---|
| per-thread size | `MemSiz 0x400001` | `size 0x400001` |
| file space used | `FileSiz 0x000000` | `offset 0`, zerofill |

## When is TLS allocated

Every access goes through the descriptor's thunk, which dyld points at `tlv_get_addr` when the image is loaded:


```text
Thread accesses libfoo's thread_local variable
        │
        ▼
Call tlv_descriptor->thunk
        │ tlv_get_addr()
        ▼
Does this thread already have TLS for libfoo?
        ├── Yes ──► Return existing address
        └── No
              │
              ▼
      Allocate libfoo TLS block
              │
              ▼
    Store it under the pthread key
              │
              ▼
        Return address
```

This is the same shape as Dynamic TLS on Linux, and macOS uses it for **every** library — not only the ones loaded with `dlopen()`.

---

# Test Setup

This experiment defines a **4 MB `thread_local` object** in `libfoo` and loads the library in two different ways:

1. startup-loaded — loaded by the dynamic loader at program startup
2. `dlopen()` — loaded at runtime

It measures when the TLS memory is actually used (VmRSS), and when the `thread_local` constructor runs.

```
memory.h           printMemory() - prints VmRSS
libfoo.cpp         thread_local variable - LargeTLS (4 MB)
                   foo() - fills the whole 4 MB buffer so the memory is really used
main_dynamic.cpp   libfoo is loaded by the dynamic loader at startup
main_dlopen.cpp    libfoo is dlopen()ed in main(), before the threads start
run.sh             build + run inside a Linux container
run_mac.sh         build + run natively on macOS
```

## Run on Linux

The tests run inside a container, since the static/dynamic TLS split is a glibc
mechanism:

```bash
./run.sh          # build and run both tests
./run.sh Release  # same, optimized
./run.sh shell    # interactive container
```

## Run on macOS

The same two programs build and run natively:

```bash
./run_mac.sh          # build and run both tests
./run_mac.sh Release  # same, optimized
```

---

# Test Results

Both programs run two threads: **t1 calls `foo()`, t2 never does.**

The `TLS constructor` and `TLS destructor` rows show where the object is created and destroyed. They mark ordering only, so they carry no numbers.

## Test on Linux

### A. Startup-loaded library (Static TLS)

| Event | VmRSS | ΔVmRSS |
|---|---:|---:|
| program start | 7,120 kB | — |
| **t1 thread start** | **11,472 kB** | **+4,352** |
| t1 calls `foo()` | — | — |
| TLS constructor (first access, inside `foo()`) | — | — |
| t1 thread end | 11,984 kB | +512 |
| TLS destructor | — | — |
| **t2 thread start** | **15,440 kB** | **+3,456** |
| t2 thread end | 15,440 kB | +0 |
| program end | 15,440 kB | +0 |

Each thread gets its own 4 MB block when it is created — including t2, which never touches the variable.

### B. `dlopen()` (Dynamic TLS)

| Event | VmRSS | ΔVmRSS |
|---|---:|---:|
| program start | 2,976 kB | — |
| after `dlopen()` | 2,976 kB | +0 |
| t1 thread start | 2,976 kB | +0 |
| t1 calls `foo()` | — | — |
| TLS constructor (first access, inside `foo()`) | — | — |
| **t1 thread end** | **7,088 kB** | **+4,112** |
| TLS destructor | — | — |
| t2 thread start | 7,216 kB | +128 |
| t2 thread end | 7,216 kB | +0 |
| after `dlclose()` | 7,216 kB | +0 |
| program end | 7,216 kB | +0 |

The 4 MB appears only when t1 first accesses the variable via `foo()`.

## Test on macOS

### A. Startup-loaded library

| Event | VmRSS | ΔVmRSS |
|---|---:|---:|
| program start | 1,216 kB | — |
| t1 thread start | 1,280 kB | +64 |
| t1 calls `foo()` | — | — |
| TLS constructor (first access, inside `foo()`) | — | — |
| **t1 thread end** | **5,440 kB** | **+4,160** |
| TLS destructor | — | — |
| t2 thread start | 5,440 kB | +0 |
| t2 thread end | 5,440 kB | +0 |
| program end | 5,408 kB | −32 |

The 4 MB appears only when t1 first accesses the variable via `foo()`.

### B. `dlopen()`

| Event | VmRSS | ΔVmRSS |
|---|---:|---:|
| program start | 1,168 kB | — |
| after `dlopen()` | 1,248 kB | +80 |
| t1 thread start | 1,296 kB | +48 |
| t1 calls `foo()` | — | — |
| TLS constructor (first access, inside `foo()`) | — | — |
| **t1 thread end** | **5,456 kB** | **+4,160** |
| TLS destructor | — | — |
| t2 thread start | 5,456 kB | +0 |
| t2 thread end | 5,456 kB | +0 |
| after `dlclose()` | 5,424 kB | −32 |
| program end | 5,424 kB | +0 |

The 4 MB appears only when t1 first accesses the variable via `foo()`.

---

# Conclusions

For Linux:
- A startup-loaded library allocates TLS when each thread is created.
- `dlopen()` allocates TLS on first access to the `thread_local` variable.

For macOS:
- Both cases allocate TLS on first access to the `thread_local` variable.

---

# Cross-checking the binaries

## ELF (Linux)

```bash
readelf -d dynamic_app | grep NEEDED   # libfoo.so appears in DT_NEEDED
readelf -d dlopen_app  | grep NEEDED   # libfoo.so does not appear
readelf -lW libfoo.so  | grep TLS      # PT_TLS segment
```

## Mach-O (macOS)

```bash
otool -L dynamic_app | grep libfoo         # @rpath/libfoo.dylib is a dependency
otool -L dlopen_app  | grep libfoo         # no match
otool -l libfoo.dylib | grep -A4 __thread  # __thread_bss 0x400001, __thread_vars 0x30
nm -mu libfoo.dylib | grep -i tlv          # __tlv_bootstrap, __tlv_atexit
```
