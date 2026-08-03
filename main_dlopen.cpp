#include "memory.h"

#include <cstdio>
#include <dlfcn.h>
#include <thread>


#ifndef LIBFOO_NAME
#define LIBFOO_NAME "libfoo.so"
#endif


void worker(void (*foo)())
{
    printMemory("thread start");

    foo();

    printMemory("after foo");
}


int main()
{
    printMemory("program start");

    void* handle = dlopen(LIBFOO_NAME, RTLD_NOW);

    if (!handle)
    {
        printf("dlopen error: %s\n", dlerror());
        return 1;
    }

    printMemory("after dlopen");

    auto foo = (void (*)())dlsym(handle, "foo");

    if (!foo)
    {
        printf("dlsym error: %s\n", dlerror());
        return 1;
    }

    std::thread t(worker, foo);

    t.join();

    dlclose(handle);

    printMemory("after dlclose");

    printMemory("program end");
}
