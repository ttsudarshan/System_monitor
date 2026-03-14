#include "NetworkMonitor.h"
#include <fstream>
#include <sstream>
#include <chrono>

uint64_t NetworkMonitor::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void NetworkMonitor::read() {
    std::ifstream f("/proc/net/dev");
    if (!f.is_open()) return;

    ifaces.clear();
    std::string line;
    // Skip header lines
    std::getline(f, line);
    std::getline(f, line);

    uint64_t curRx = 0, curTx = 0;

    while (std::getline(f, line)) {
        // Format: "  iface: rx_bytes rx_packets ... tx_bytes tx_packets ..."
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string name = line.substr(0, colon);
        // Trim whitespace
        size_t start = name.find_first_not_of(' ');
        if (start != std::string::npos) name = name.substr(start);

        // Skip loopback
        if (name == "lo") continue;

        std::istringstream ss(line.substr(colon + 1));
        uint64_t rxBytes, rxPackets, rxErrs, rxDrop, rxFifo, rxFrame, rxCompressed, rxMulticast;
        uint64_t txBytes;
        ss >> rxBytes >> rxPackets >> rxErrs >> rxDrop >> rxFifo >> rxFrame >> rxCompressed >> rxMulticast;
        ss >> txBytes;

        IfaceInfo info;
        info.name = name;
        info.rxBytes = rxBytes;
        info.txBytes = txBytes;
        ifaces.push_back(info);

        curRx += rxBytes;
        curTx += txBytes;
    }

    totalRx = curRx;
    totalTx = curTx;

    uint64_t now = nowMs();
    if (prevTimestamp > 0) {
        double elapsed = (now - prevTimestamp) / 1000.0;
        if (elapsed > 0) {
            rxRate = (curRx - prevRx) / elapsed;
            txRate = (curTx - prevTx) / elapsed;
            if (rxRate < 0) rxRate = 0;
            if (txRate < 0) txRate = 0;
        }
    }

    prevRx = curRx;
    prevTx = curTx;
    prevTimestamp = now;
}