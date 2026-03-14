#pragma once

#include <string>
#include <vector>
#include <cstdint>

class DiskMonitor {
public:
    DiskMonitor() = default;

    void read();

    struct DiskInfo {
        std::string device;
        std::string mountPoint;
        std::string fsType;
        uint64_t totalMB;
        uint64_t usedMB;
        uint64_t availMB;
        double usagePercent;
    };

    // Filesystem usage from df-like data
    const std::vector<DiskInfo>& disks() const { return diskList; }

    // I/O stats (bytes/sec since last read)
    double readBytesPerSec() const { return readRate; }
    double writeBytesPerSec() const { return writeRate; }

private:
    std::vector<DiskInfo> diskList;

    uint64_t prevReadBytes = 0;
    uint64_t prevWriteBytes = 0;
    uint64_t prevTimestamp = 0;

    double readRate = 0.0;
    double writeRate = 0.0;

    void readFilesystems();
    void readIO();
    uint64_t nowMs();
};