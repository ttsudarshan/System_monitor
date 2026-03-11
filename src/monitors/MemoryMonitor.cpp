#include "MemoryMonitor.h"
#include <fstream>
#include <sstream>
#include <string>

void MemoryMonitor::read() {
    std::ifstream f("/proc/meminfo");
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string key;
        uint64_t val;
        ss >> key >> val; // value is in kB

        if (key == "MemTotal:")       total = val;
        else if (key == "MemAvailable:") available = val;
        else if (key == "SwapTotal:")    swapTotal = val;
        else if (key == "SwapFree:")     swapFree = val;
    }
}