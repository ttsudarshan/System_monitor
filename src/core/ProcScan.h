#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct ProcInfo {
    int pid = 0;
    int ppid = 0;
    unsigned uid = 0;
    std::string comm;       // name from /proc/[pid]/stat
    std::string user;
    std::string cmdline;    // arguments joined by spaces
    std::string app;        // canonical app name (AppNames::canonical(comm))
    char state = '?';
    int nice = 0;
    double cpuPct = 0;      // percent of one core, 0 .. 100 * cores
    double memMB = 0;       // resident set size
    uint64_t cpuDelta = 0;  // clock ticks used since the previous scan
};

struct ProcScanResult {
    std::vector<ProcInfo> procs;
    uint64_t sysDelta = 0;  // total clock ticks elapsed on all CPUs since previous scan
    int numCpus = 1;
    // canonical app -> fraction of all CPU time (0..1) used since the previous scan
    std::unordered_map<std::string, double> appCpuShare;
};

// One pass over /proc that serves the Processes tab and the energy attribution.
class ProcScanner {
public:
    ProcScanner();
    ProcScanResult scan();

private:
    struct Prev {
        uint64_t ticks = 0;
        uint64_t startTime = 0;  // detects PID reuse
    };
    std::unordered_map<int, Prev> prev_;
    uint64_t prevTotal_ = 0;
    std::unordered_map<unsigned, std::string> userCache_;
    long pageSizeKB_ = 4;

    static uint64_t readTotalCpuTicks();
};
