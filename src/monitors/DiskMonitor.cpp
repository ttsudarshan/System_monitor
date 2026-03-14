#include "DiskMonitor.h"
#include <fstream>
#include <sstream>
#include <sys/statvfs.h>
#include <chrono>

uint64_t DiskMonitor::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void DiskMonitor::read() {
    readFilesystems();
    readIO();
}

void DiskMonitor::readFilesystems() {
    diskList.clear();
    std::ifstream f("/proc/mounts");
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string dev, mount, fs;
        ss >> dev >> mount >> fs;

        // Only show real block devices
        if (dev.find("/dev/") != 0) continue;
        if (dev.find("/dev/loop") == 0) continue;

        struct statvfs stat;
        if (statvfs(mount.c_str(), &stat) != 0) continue;

        DiskInfo d;
        d.device = dev;
        d.mountPoint = mount;
        d.fsType = fs;
        d.totalMB = (stat.f_blocks * stat.f_frsize) / (1024 * 1024);
        d.availMB = (stat.f_bavail * stat.f_frsize) / (1024 * 1024);
        d.usedMB = d.totalMB - d.availMB;
        d.usagePercent = (d.totalMB > 0)
            ? (static_cast<double>(d.usedMB) / d.totalMB) * 100.0 : 0.0;

        diskList.push_back(d);
    }
}

void DiskMonitor::readIO() {
    // Read from /proc/diskstats
    std::ifstream f("/proc/diskstats");
    if (!f.is_open()) return;

    uint64_t totalRead = 0, totalWrite = 0;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        uint64_t major, minor;
        std::string name;
        ss >> major >> minor >> name;

        // Only count whole-disk devices (sd*, nvme*n*, vd*)
        // Skip partitions like sda1, nvme0n1p1
        bool isDisk = false;
        if (name.find("sd") == 0 && name.length() == 3) isDisk = true;
        if (name.find("nvme") == 0 && name.find('p') == std::string::npos) isDisk = true;
        if (name.find("vd") == 0 && name.length() == 3) isDisk = true;
        if (!isDisk) continue;

        // Fields: reads_completed, reads_merged, sectors_read, ms_reading,
        //         writes_completed, writes_merged, sectors_written, ...
        uint64_t rc, rm, sr, msr, wc, wm, sw;
        ss >> rc >> rm >> sr >> msr >> wc >> wm >> sw;

        totalRead += sr * 512;   // sectors are 512 bytes
        totalWrite += sw * 512;
    }

    uint64_t now = nowMs();
    if (prevTimestamp > 0) {
        double elapsed = (now - prevTimestamp) / 1000.0;
        if (elapsed > 0) {
            readRate = (totalRead - prevReadBytes) / elapsed;
            writeRate = (totalWrite - prevWriteBytes) / elapsed;
            if (readRate < 0) readRate = 0;
            if (writeRate < 0) writeRate = 0;
        }
    }

    prevReadBytes = totalRead;
    prevWriteBytes = totalWrite;
    prevTimestamp = now;
}