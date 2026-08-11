#include "memory.h"

#include <cstdio>
#include <dlfcn.h>
#include <mutex>
#include <thread>


#ifndef LIBFOO_NAME
#define LIBFOO_NAME "libfoo.so"
#endif


std::mutex worker_mutex;


void worker(void (*foo)())
{
    std::lock_guard<std::mutex> lock(worker_mutex);

    printMemory("t1 thread start");

    foo();

    printMemory("t1 thread end");
}


void workerNoFooCalled()
{
    std::lock_guard<std::mutex> lock(worker_mutex);

    printMemory("t2 thread start");
    printMemory("t2 thread end");
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

    std::thread t1(worker, foo);
    std::thread t2(workerNoFooCalled);

    t1.join();
    t2.join();

    dlclose(handle);

    printMemory("after dlclose");

    printMemory("program end");
}
