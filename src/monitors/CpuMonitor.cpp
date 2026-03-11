#include "CpuMonitor.h"
#include <fstream>
#include <sstream>

CpuMonitor::CpuTimes CpuMonitor::parseLine(const std::string &line) {
    CpuTimes t;
    std::istringstream ss(line);
    std::string label;
    ss >> label >> t.user >> t.nice >> t.system >> t.idle
       >> t.iowait >> t.irq >> t.softirq >> t.steal;
    return t;
}

void CpuMonitor::read() {
    std::ifstream f("/proc/stat");
    if (!f.is_open()) return;

    std::string line;
    std::vector<CpuTimes> cores;
    CpuTimes totalCpu;
    bool first = true;

    while (std::getline(f, line)) {
        if (line.substr(0, 3) != "cpu") break;

        if (first) {
            // "cpu " line = aggregate
            totalCpu = parseLine(line);
            first = false;
        } else {
            // "cpu0", "cpu1", ... lines
            cores.push_back(parseLine(line));
        }
    }

    // Compute overall usage delta
    auto delta = [](const CpuTimes &cur, const CpuTimes &prev) -> double {
        uint64_t totD = cur.total() - prev.total();
        uint64_t actD = cur.totalActive() - prev.totalActive();
        return (totD == 0) ? 0.0 : (static_cast<double>(actD) / totD) * 100.0;
    };

    if (prevOverall.total() > 0) {
        overall = delta(totalCpu, prevOverall);
    }
    prevOverall = totalCpu;

    // Per-core
    coreUsage.resize(cores.size(), 0.0);
    if (prevCores.size() == cores.size()) {
        for (size_t i = 0; i < cores.size(); ++i) {
            coreUsage[i] = delta(cores[i], prevCores[i]);
        }
    }
    prevCores = cores;
}

double CpuMonitor::overallUsage() const { return overall; }
const std::vector<double>& CpuMonitor::perCoreUsage() const { return coreUsage; }