#pragma once

#include <cstdint>
#include <string>
#include <vector>

class NetworkMonitor {
public:
    NetworkMonitor() = default;

    void read();

    struct IfaceInfo {
        std::string name;
        uint64_t rxBytes;
        uint64_t txBytes;
    };

    double rxBytesPerSec() const { return rxRate; }
    double txBytesPerSec() const { return txRate; }
    uint64_t totalRxBytes() const { return totalRx; }
    uint64_t totalTxBytes() const { return totalTx; }
    const std::vector<IfaceInfo>& interfaces() const { return ifaces; }

private:
    std::vector<IfaceInfo> ifaces;
    uint64_t prevRx = 0, prevTx = 0;
    uint64_t prevTimestamp = 0;
    uint64_t totalRx = 0, totalTx = 0;
    double rxRate = 0.0, txRate = 0.0;
    uint64_t nowMs();
};