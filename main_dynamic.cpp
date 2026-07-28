#include "memory.h"

#include <thread>


extern "C" void foo();


void worker()
{
    printMemory("thread start");

    foo();

    printMemory("after foo");
}


int main()
{
    printMemory("program start");

    std::thread t(worker);

    t.join();

    printMemory("program end");
}
