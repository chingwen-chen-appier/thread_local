# Background: Static TLS vs. Dynamic TLS

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

On Linux, TLS is implemented by **glibc** together with the dynamic loader (`ld.so`). Depending on **when a shared library becomes known to the runtime**, its TLS is managed as either **Static TLS** or **Dynamic TLS**.

## Static TLS

At process startup, the dynamic loader (`ld.so`) loads the main executable and all shared libraries listed in the executable's `DT_NEEDED` entries.

If an object contains `thread_local` variables, its ELF file includes a `PT_TLS` program header describing its TLS segment.

This project's `libfoo.so` defines a single 4 MB `thread_local` object, and its `PT_TLS` header looks like this:

```text
Program Headers:                     (readelf -lW libfoo.so, columns abridged)

  Type           Offset   VirtAddr           FileSiz  MemSiz   Flg Align
  ...
  TLS            0x00fd90 0x000000000001fd90 0x000000 0x400001 R   0x8
```

Two things to read off it:

- `MemSiz` is `0x400001` — the per-thread footprint. That is the 4 MB object plus one byte: a `__tls_guard` flag the compiler adds to record whether the constructor has already run for this thread.
- `FileSiz` is `0x000000`. The object has no initialized data, so it lives entirely in `.tbss` and is zero-filled per thread rather than copied from the file.

By scanning the `PT_TLS` program headers of all objects loaded at startup, the dynamic loader learns the size, alignment, and initial contents of each object's TLS segment.

Suppose the program starts with:

```text
main executable
libfoo.so
```

When `pthread_create()` creates a new thread, glibc allocates the thread's runtime data structures, including a static TLS block large enough to hold the TLS segments of all startup-loaded objects:

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

Dynamic TLS is used for shared libraries loaded **after** process startup, typically via `dlopen()`.

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

Instead, TLS for `libfoo.so` is allocated lazily when a thread first accesses one of its `thread_local` variables:

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

# Test

This experiment defines a **4 MB `thread_local` object** in `libfoo.so` and loads the library in two different ways:

1. Linked at program startup (`DT_NEEDED`) → **Static TLS**
2. Loaded with `dlopen()` at runtime → **Dynamic TLS**

The experiment measures:

- when the TLS memory becomes resident (VmRSS)
- when the `thread_local` constructor runs

```
memory.h           printMemory() prints VmRSS
libfoo.cpp         thread_local LargeTLS (4 MB, ctor/dtor)
main_dynamic.cpp   libfoo is a DT_NEEDED dependency (Static TLS)
main_dlopen.cpp    libfoo is loaded via dlopen() (Dynamic TLS)
run.sh             build + run inside a Linux container
```

macOS does not expose `/proc` and does not implement the same static/dynamic TLS mechanism as glibc, so the experiment is run inside a Linux container:

```bash
./run.sh          # build and run both tests
./run.sh shell    # interactive container (pmap / gdb available)
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
| after `foo()` | 11,176 kB | +0 | No additional pages are allocated because the TLS memory already exists. |
| TLS destructor | — | — | Destructor runs when the thread exits. Ordering only. |
| program end | 11,176 kB | +0 | Memory remains cached by glibc's thread stack cache. |

## B. `dlopen()` (`Dynamic TLS`)

| Event | VmRSS | ΔVmRSS | What happens |
|---|---:|---:|---|
| program start | 2,956 kB | — | `libfoo.so` is not loaded yet. |
| thread start | 2,956 kB | +0 | Thread is created without TLS for `libfoo.so`. |
| after `dlopen()` | 2,956 kB | +0 | `libfoo.so` is loaded and registered as a TLS module, but no per-thread TLS memory is allocated. |
| TLS constructor | — | — | First call to `__tls_get_addr()` allocates and zero-initializes the 4 MB TLS block, then runs the constructor. Ordering only. |
| after `foo()` | 7,132 kB | +4,176 | TLS allocation occurred during the first access. |
| after `dlclose()` | 7,132 kB | +0 | `dlclose()` returns 0 but does not actually unload `libfoo.so`, so nothing is freed and no destructor runs. See below. |
| TLS destructor | — | — | Destructor runs when the thread exits, not at `dlclose()`. Ordering only. |
| program end | 7,132 kB | +0 | Memory remains attached until the thread terminates and may be retained in glibc's thread cache. |

---

# Conclusions

- **Static TLS** is allocated when each thread is created.
- **Dynamic TLS** is allocated lazily, the first time each thread accesses a `thread_local` variable from a library loaded with `dlopen()`.

For the same 4 MB `thread_local` object:

| Static TLS | Dynamic TLS |
|------------|-------------|
| Allocation happens during `pthread_create()` | Allocation happens on the first `thread_local` access (`__tls_get_addr()`) |
| `foo()` does not increase RSS | `foo()` triggers the RSS increase |
| Constructor still runs on first access | Constructor still runs on first access |

---

# Cross-checking with ELF

```bash
readelf -d dynamic_app | grep NEEDED   # libfoo.so appears in DT_NEEDED
readelf -d dlopen_app  | grep NEEDED   # libfoo.so does not appear
readelf -lW libfoo.so  | grep TLS      # PT_TLS segment
```