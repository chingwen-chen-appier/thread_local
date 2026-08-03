#pragma once

#include <iostream>


#if defined(__APPLE__)

#include <mach/mach.h>


inline void printMemory(const char* tag)
{
    std::cout << "\n==== " << tag << " ====" << std::endl;

    mach_task_basic_info_data_t info;

    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;

    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) == KERN_SUCCESS)
    {
        std::cout << "VmRSS:\t" << info.resident_size / 1024 << " kB" << std::endl;
    }
}

#else

#include <fstream>
#include <string>


inline void printMemory(const char* tag)
{
    std::cout << "\n==== " << tag << " ====" << std::endl;

    std::ifstream status("/proc/self/status");

    std::string line;

    while (std::getline(status, line))
    {
        if (line.find("VmRSS") == 0)
        {
            std::cout << line << std::endl;
        }
    }
}

#endif
