#include "memory.h"

#include <mutex>
#include <thread>


extern "C" void foo();


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

    std::thread t1(worker, foo);
    std::thread t2(workerNoFooCalled);

    t1.join();
    t2.join();

    printMemory("program end");
}
