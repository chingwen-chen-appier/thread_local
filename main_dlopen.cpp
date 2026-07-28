#include "memory.h"

#include <cstdio>
#include <dlfcn.h>
#include <thread>


void worker()
{
    printMemory("thread start");

    void* handle = dlopen("libfoo.so", RTLD_NOW);

    if (!handle)
    {
        printf("dlopen error: %s\n", dlerror());
        return;
    }

    printMemory("after dlopen");

    auto foo = (void (*)())dlsym(handle, "foo");

    foo();

    printMemory("after foo");

    dlclose(handle);

    printMemory("after dlclose");
}


int main()
{
    printMemory("program start");

    std::thread t(worker);

    t.join();

    printMemory("program end");
}
