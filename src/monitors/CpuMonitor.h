#pragma once

#include <vector>
#include <string>
#include <cstdint>

class CpuMonitor {
public:
    CpuMonitor() = default;

    // Read /proc/stat and compute usage since last read
    void read();

    // Overall CPU usage as percentage (0-100)
    double overallUsage() const;

    // Per-core usage as percentages
    const std::vector<double>& perCoreUsage() const;

    int coreCount() const { return static_cast<int>(coreUsage.size()); }

private:
    struct CpuTimes {
        uint64_t user = 0, nice = 0, system = 0, idle = 0;
        uint64_t iowait = 0, irq = 0, softirq = 0, steal = 0;
        uint64_t totalActive() const { return user + nice + system + irq + softirq + steal; }
        uint64_t totalIdle() const { return idle + iowait; }
        uint64_t total() const { return totalActive() + totalIdle(); }
    };

    static CpuTimes parseLine(const std::string &line);

    CpuTimes prevOverall;
    std::vector<CpuTimes> prevCores;

    double overall = 0.0;
    std::vector<double> coreUsage;
};