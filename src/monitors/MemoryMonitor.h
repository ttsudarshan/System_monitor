#pragma once

#include <cstdint>

class MemoryMonitor {
public:
    MemoryMonitor() = default;

    void read();

    double totalMB() const { return total / 1024.0; }
    double usedMB() const { return (total - available) / 1024.0; }
    double swapTotalMB() const { return swapTotal / 1024.0; }
    double swapUsedMB() const { return (swapTotal - swapFree) / 1024.0; }

    double usagePercent() const {
        return (total == 0) ? 0.0 : (1.0 - static_cast<double>(available) / total) * 100.0;
    }

private:
    uint64_t total = 0;
    uint64_t available = 0;
    uint64_t swapTotal = 0;
    uint64_t swapFree = 0;
};