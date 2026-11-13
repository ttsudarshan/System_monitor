#include "EnergyMonitor.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

uint64_t EnergyMonitor::nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool EnergyMonitor::readCounter(const std::string &path, uint64_t &out) {
    if (path.empty()) return false;
    std::ifstream f(path);
    return static_cast<bool>(f >> out);
}

// ─── CPU detection ──────────────────────────────────────────────

void EnergyMonitor::detectCpu() {
    std::ifstream f("/proc/cpuinfo");
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("vendor_id") != std::string::npos) {
            if (line.find("GenuineIntel") != std::string::npos) cpuVendor = CpuVendor::Intel;
            else if (line.find("AuthenticAMD") != std::string::npos) cpuVendor = CpuVendor::AMD;
        }
        if (line.find("model name") != std::string::npos && modelName.empty()) {
            auto pos = line.find(':');
            if (pos != std::string::npos && pos + 2 <= line.size()) modelName = line.substr(pos + 2);
        }
    }
    numCores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
}

std::string EnergyMonitor::vendorName() const {
    switch (cpuVendor) {
        case CpuVendor::Intel: return "Intel";
        case CpuVendor::AMD: return "AMD";
        default: return "Unknown";
    }
}

// ─── RAPL ───────────────────────────────────────────────────────

void EnergyMonitor::initRapl() {
    const char *const candidates[] = {
        "/sys/class/powercap/intel-rapl:0",
        "/sys/class/powercap/amd-rapl:0",
        "/sys/class/powercap/intel-rapl/intel-rapl:0",
        nullptr};

    for (int i = 0; candidates[i]; ++i) {
        std::string base = candidates[i];
        uint64_t v = 0;
        // Existence is not enough: energy_uj is root-only unless a udev rule opens it.
        if (!readCounter(base + "/energy_uj", v)) continue;

        packagePath_ = base + "/energy_uj";
        readCounter(base + "/max_energy_range_uj", raplMaxEnergy);

        // Sub-domains (core, dram, uncore) live in intel-rapl:0:N with a `name` file.
        try {
            for (auto &entry : fs::directory_iterator(base)) {
                if (!fs::is_directory(entry)) continue;
                std::ifstream nf(entry.path().string() + "/name");
                std::string name;
                if (!std::getline(nf, name)) continue;
                std::string counter = entry.path().string() + "/energy_uj";
                if (name == "core") corePath_ = counter;
                else if (name == "dram") dramPath_ = counter;
            }
        } catch (...) {}

        readCounter(packagePath_, prevPackageEnergy);
        readCounter(corePath_, prevCoreEnergy);
        readCounter(dramPath_, prevDramEnergy);
        prevTimestampUs = nowUs();
        raplAvailable = true;
        return;
    }
}

void EnergyMonitor::initAmdEnergy() {
    // Older AMD kernels expose counters through the amd_energy hwmon driver.
    try {
        for (auto &entry : fs::directory_iterator("/sys/class/hwmon")) {
            std::ifstream nf(entry.path().string() + "/name");
            std::string name;
            if (std::getline(nf, name) && name == "amd_energy") {
                std::string counter = entry.path().string() + "/energy1_input";
                if (!readCounter(counter, prevPackageEnergy)) continue;
                packagePath_ = counter;
                prevTimestampUs = nowUs();
                raplAvailable = true;
                return;
            }
        }
    } catch (...) {}
}

void EnergyMonitor::readRaplPower() {
    if (!raplAvailable) return;

    uint64_t now = nowUs();
    uint64_t curPackage = 0, curCore = 0, curDram = 0;
    bool havePkg = readCounter(packagePath_, curPackage);
    bool haveCore = readCounter(corePath_, curCore);
    bool haveDram = readCounter(dramPath_, curDram);

    double elapsed = (now - prevTimestampUs) / 1e6;
    if (!havePkg || elapsed <= 0 || prevTimestampUs == 0) {
        prevPackageEnergy = curPackage; prevCoreEnergy = curCore; prevDramEnergy = curDram;
        prevTimestampUs = now;
        return;
    }

    auto delta = [this](uint64_t cur, uint64_t prev) -> uint64_t {
        if (cur >= prev) return cur - prev;
        // Counter wrapped. Without a known range (hwmon) we cannot tell by how much.
        return raplMaxEnergy > 0 && raplMaxEnergy > prev ? (raplMaxEnergy - prev) + cur : 0;
    };

    packagePower = delta(curPackage, prevPackageEnergy) / (elapsed * 1e6);
    corePower = haveCore ? delta(curCore, prevCoreEnergy) / (elapsed * 1e6) : 0;
    dramPower = haveDram ? delta(curDram, prevDramEnergy) / (elapsed * 1e6) : 0;

    // RAPL occasionally returns nonsense right after resume.
    if (packagePower > 500.0) packagePower = 0;
    if (corePower > 500.0) corePower = 0;
    if (dramPower > 500.0) dramPower = 0;

    prevPackageEnergy = curPackage; prevCoreEnergy = curCore; prevDramEnergy = curDram;
    prevTimestampUs = now;
}

// ─── Attribution ────────────────────────────────────────────────

void EnergyMonitor::attribute(const ProcScanResult &scan, double elapsedSec) {
    if (raplAvailable && packagePower > 0) {
        powerSource_ = PowerSource::Rapl;
        attributedPower_ = packagePower;
    } else if (battery_.hasBattery() && battery_.isDischarging() && battery_.powerDrawWatts() > 0) {
        powerSource_ = PowerSource::Battery;
        attributedPower_ = battery_.powerDrawWatts();
    } else {
        // Nothing measures consumption right now (AC power, no RAPL access).
        // Report CPU shares only rather than inventing a wattage.
        powerSource_ = PowerSource::None;
        attributedPower_ = 0;
    }

    double totalShare = 0;
    for (const auto &[app, share] : scan.appCpuShare) totalShare += share;

    for (auto &[app, accum] : appEnergy) {
        accum.currentWatts = 0;
        accum.currentPercent = 0;
    }
    if (totalShare <= 0 || scan.sysDelta == 0) return;

    for (const auto &[app, share] : scan.appCpuShare) {
        double fraction = share / totalShare;
        AppAccum &a = appEnergy[app];
        a.currentPercent = fraction * 100.0;
        a.currentWatts = fraction * attributedPower_;
        double joules = a.currentWatts * elapsedSec;
        a.totalJoules += joules;
        a.pendingWh += joules / 3600.0;
    }
}

std::vector<EnergyMonitor::AppEnergy> EnergyMonitor::perAppEnergy() const {
    std::vector<AppEnergy> result;
    for (const auto &[app, accum] : appEnergy) {
        if (accum.totalJoules < 0.001 && accum.currentPercent < 0.05) continue;
        result.push_back({app, accum.currentWatts, accum.currentPercent, accum.totalJoules});
    }
    std::sort(result.begin(), result.end(), [](const AppEnergy &a, const AppEnergy &b) {
        if (a.watts != b.watts) return a.watts > b.watts;
        return a.percent > b.percent;
    });
    return result;
}

std::vector<std::pair<std::string, double>> EnergyMonitor::takePendingWh() {
    std::vector<std::pair<std::string, double>> out;
    for (auto &[app, accum] : appEnergy) {
        if (accum.pendingWh > 0) {
            out.emplace_back(app, accum.pendingWh);
            accum.pendingWh = 0;
        }
    }
    return out;
}

// ─── Constructor & main read ────────────────────────────────────

EnergyMonitor::EnergyMonitor() {
    detectCpu();
    initRapl();
    if (!raplAvailable) initAmdEnergy();
    battery_.read();
}

void EnergyMonitor::read(const ProcScanResult &scan, double elapsedSec) {
    readRaplPower();
    battery_.read();
    attribute(scan, elapsedSec);
}
