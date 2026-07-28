#pragma once

#include <fstream>
#include <iostream>
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
