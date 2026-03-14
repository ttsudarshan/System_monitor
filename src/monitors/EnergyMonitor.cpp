#include "EnergyMonitor.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <unistd.h>
#include <dirent.h>
#include <cstring>

namespace fs = std::filesystem;

uint64_t EnergyMonitor::nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ─── CPU Detection ──────────────────────────────────────────────

void EnergyMonitor::detectCpu() {
    std::ifstream f("/proc/cpuinfo");
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        if (line.find("vendor_id") != std::string::npos) {
            if (line.find("GenuineIntel") != std::string::npos)
                cpuVendor = CpuVendor::Intel;
            else if (line.find("AuthenticAMD") != std::string::npos)
                cpuVendor = CpuVendor::AMD;
        }
        if (line.find("model name") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                modelName = line.substr(pos + 2);
            }
        }
    }

    numCores = sysconf(_SC_NPROCESSORS_ONLN);
}

std::string EnergyMonitor::vendorName() const {
    switch (cpuVendor) {
        case CpuVendor::Intel: return "Intel";
        case CpuVendor::AMD: return "AMD";
        default: return "Unknown";
    }
}

// ─── RAPL / AMD Energy ──────────────────────────────────────────

void EnergyMonitor::initRapl() {
    // Intel RAPL: /sys/class/powercap/intel-rapl:0/
    // AMD RAPL:   /sys/class/powercap/amd-rapl:0/ (kernel 5.8+)
    //             or same intel-rapl path on some AMD systems

    std::vector<std::string> paths = {
        "/sys/class/powercap/intel-rapl:0",
        "/sys/class/powercap/amd-rapl:0",
        "/sys/class/powercap/intel-rapl/intel-rapl:0"
    };

    for (const auto &p : paths) {
        if (fs::exists(p + "/energy_uj")) {
            raplBasePath = p;
            raplAvailable = true;

            // Read max energy range
            std::ifstream mf(p + "/max_energy_range_uj");
            if (mf.is_open()) mf >> raplMaxEnergy;

            // Initial reading
            prevPackageEnergy = readRaplEnergy("");
            prevCoreEnergy = readRaplEnergy("core");
            prevDramEnergy = readRaplEnergy("dram");
            prevTimestamp = nowUs();
            break;
        }
    }
}

void EnergyMonitor::initAmdEnergy() {
    // AMD Energy Driver: /sys/class/hwmon/hwmonX/
    // Look for amd_energy driver
    if (raplAvailable) return; // Already have RAPL

    try {
        for (auto &entry : fs::directory_iterator("/sys/class/hwmon")) {
            std::string namePath = entry.path().string() + "/name";
            std::ifstream nf(namePath);
            if (nf.is_open()) {
                std::string name;
                std::getline(nf, name);
                if (name == "amd_energy") {
                    // Found AMD energy driver
                    // It exposes energy counters via energyX_input (in microjoules)
                    raplBasePath = entry.path().string();
                    raplAvailable = true;
                    prevTimestamp = nowUs();
                    // Read initial package energy (typically energy1_input)
                    std::ifstream ef(raplBasePath + "/energy1_input");
                    if (ef.is_open()) ef >> prevPackageEnergy;
                    break;
                }
            }
        }
    } catch (...) {}
}

uint64_t EnergyMonitor::readRaplEnergy(const std::string &domain) {
    std::string path;

    if (domain.empty()) {
        // Package energy
        path = raplBasePath + "/energy_uj";
    } else {
        // Subdomain: look for intel-rapl:0:X where name matches
        try {
            for (auto &entry : fs::directory_iterator(raplBasePath)) {
                if (!fs::is_directory(entry)) continue;
                std::string namePath = entry.path().string() + "/name";
                std::ifstream nf(namePath);
                if (nf.is_open()) {
                    std::string name;
                    std::getline(nf, name);
                    if (name == domain) {
                        path = entry.path().string() + "/energy_uj";
                        break;
                    }
                }
            }
        } catch (...) {}
    }

    if (path.empty()) return 0;

    std::ifstream f(path);
    if (!f.is_open()) return 0;
    uint64_t val = 0;
    f >> val;
    return val;
}

void EnergyMonitor::readRaplPower() {
    if (!raplAvailable) return;

    uint64_t now = nowUs();
    uint64_t curPackage = readRaplEnergy("");
    uint64_t curCore = readRaplEnergy("core");
    uint64_t curDram = readRaplEnergy("dram");

    double elapsed = (now - prevTimestamp) / 1000000.0; // seconds
    if (elapsed <= 0 || prevTimestamp == 0) {
        prevPackageEnergy = curPackage;
        prevCoreEnergy = curCore;
        prevDramEnergy = curDram;
        prevTimestamp = now;
        return;
    }

    // Handle counter overflow
    auto delta = [this](uint64_t cur, uint64_t prev) -> uint64_t {
        if (cur >= prev) return cur - prev;
        return (static_cast<uint64_t>(raplMaxEnergy) - prev) + cur;
    };

    uint64_t pkgDelta = delta(curPackage, prevPackageEnergy);
    uint64_t coreDelta = delta(curCore, prevCoreEnergy);
    uint64_t dramDelta = delta(curDram, prevDramEnergy);

    // Convert microjoules to watts: W = uJ / (seconds * 1e6)
    packagePower = pkgDelta / (elapsed * 1000000.0);
    corePower = coreDelta / (elapsed * 1000000.0);
    dramPower = dramDelta / (elapsed * 1000000.0);

    // Sanity check — RAPL sometimes gives garbage on resume
    if (packagePower > 500.0) packagePower = 0;
    if (corePower > 500.0) corePower = 0;
    if (dramPower > 500.0) dramPower = 0;

    prevPackageEnergy = curPackage;
    prevCoreEnergy = curCore;
    prevDramEnergy = curDram;
    prevTimestamp = now;
}

// ─── Battery ────────────────────────────────────────────────────

void EnergyMonitor::readBattery() {
    batteryPresent = false;
    std::string base = "/sys/class/power_supply/";

    try {
        for (auto &entry : fs::directory_iterator(base)) {
            std::string name = entry.path().filename().string();
            if (name.find("BAT") != 0) continue;

            batteryPresent = true;
            std::string p = entry.path().string() + "/";

            std::ifstream capF(p + "capacity");
            if (capF.is_open()) capF >> battLevel;

            std::ifstream statF(p + "status");
            if (statF.is_open()) std::getline(statF, battStatus);

            // Power draw
            std::ifstream powF(p + "power_now");
            if (powF.is_open()) {
                uint64_t uW; powF >> uW;
                battPower = uW / 1000000.0;
            } else {
                std::ifstream vf(p + "voltage_now");
                std::ifstream cf(p + "current_now");
                if (vf.is_open() && cf.is_open()) {
                    uint64_t uV, uA;
                    vf >> uV; cf >> uA;
                    battPower = (uV / 1e6) * (uA / 1e6);
                }
            }

            // Energy remaining
            std::ifstream enF(p + "energy_now");
            if (enF.is_open()) {
                uint64_t uWh; enF >> uWh;
                battEnergyWh = uWh / 1000000.0;
            }

            std::ifstream ecF(p + "energy_full");
            if (ecF.is_open()) {
                uint64_t uWh; ecF >> uWh;
                battCapacityWh = uWh / 1000000.0;
            }

            break;
        }
    } catch (...) {
        batteryPresent = false;
    }
}

int EnergyMonitor::estimatedMinutesRemaining() const {
    if (!batteryPresent || battPower <= 0.1 || battStatus != "Discharging")
        return -1;
    double hoursLeft = battEnergyWh / battPower;
    return static_cast<int>(hoursLeft * 60);
}

// ─── Per-App Energy Attribution ─────────────────────────────────

uint64_t EnergyMonitor::readTotalCpuTime() {
    std::ifstream f("/proc/stat");
    if (!f.is_open()) return 0;
    std::string line;
    std::getline(f, line);
    std::istringstream ss(line);
    std::string label;
    ss >> label;
    uint64_t total = 0, val;
    while (ss >> val) total += val;
    return total;
}

std::string EnergyMonitor::normalizeAppName(const std::string &comm) {
    std::string n = comm;
    for (auto &c : n) c = tolower(c);

    if (n.find("brave") != std::string::npos) return "Brave Browser";
    if (n.find("firefox") != std::string::npos) return "Firefox";
    if (n.find("chrome") != std::string::npos && n.find("chromium") == std::string::npos) return "Google Chrome";
    if (n.find("chromium") != std::string::npos) return "Chromium";
    if (n == "code" || n.find("code") == 0) return "VS Code";
    if (n.find("slack") != std::string::npos) return "Slack";
    if (n.find("discord") != std::string::npos) return "Discord";
    if (n.find("spotify") != std::string::npos) return "Spotify";
    if (n.find("telegram") != std::string::npos) return "Telegram";
    if (n.find("thunderbird") != std::string::npos) return "Thunderbird";
    if (n.find("libreoffice") != std::string::npos) return "LibreOffice";
    if (n.find("gimp") != std::string::npos) return "GIMP";
    if (n.find("vlc") != std::string::npos) return "VLC";
    if (n.find("steam") != std::string::npos) return "Steam";
    if (n.find("nemo") != std::string::npos) return "Files";
    if (n.find("cinnamon") != std::string::npos) return "Desktop";
    if (n.find("xorg") != std::string::npos || n.find("xwayland") != std::string::npos) return "Display Server";
    if (n.find("pulseaudio") != std::string::npos || n.find("pipewire") != std::string::npos) return "Audio System";
    if (n.find("networkmanager") != std::string::npos) return "Network Manager";
    if (n.find("systemd") != std::string::npos) return "System Services";
    if (n.find("kworker") != std::string::npos) return "Kernel Workers";
    return comm;
}

void EnergyMonitor::computePerAppEnergy() {
    uint64_t totalCpuNow = readTotalCpuTime();
    if (prevTotalCpu == 0) {
        prevTotalCpu = totalCpuNow;
        // First pass: collect baselines
        DIR *dir = opendir("/proc");
        if (!dir) return;
        struct dirent *ent;
        while ((ent = readdir(dir))) {
            int pid;
            try { pid = std::stoi(ent->d_name); } catch (...) { continue; }

            std::ifstream sf(std::string("/proc/") + ent->d_name + "/stat");
            if (!sf.is_open()) continue;
            std::string line;
            std::getline(sf, line);
            auto lp = line.find('(');
            auto rp = line.rfind(')');
            if (lp == std::string::npos || rp == std::string::npos) continue;

            std::string comm = line.substr(lp + 1, rp - lp - 1);
            std::istringstream rest(line.substr(rp + 2));
            std::string state;
            uint64_t dummy, utime, stime;
            rest >> state;
            for (int i = 0; i < 10; ++i) rest >> dummy;
            rest >> utime >> stime;

            PidCpuSnap s;
            s.appName = normalizeAppName(comm);
            s.cpuTime = utime + stime;
            prevPidSnap[pid] = s;
        }
        closedir(dir);
        return;
    }

    uint64_t sysDelta = totalCpuNow - prevTotalCpu;
    if (sysDelta == 0) return;

    // Determine total power to attribute
    double totalPower = 0;
    if (raplAvailable && packagePower > 0) {
        totalPower = packagePower; // Use RAPL measurement
    } else if (batteryPresent && battPower > 0) {
        totalPower = battPower;    // Fallback to battery reading
    } else {
        totalPower = 15.0;        // Rough estimate for unknown systems
    }

    // Track per-app CPU share
    std::unordered_map<std::string, double> appCpuShare;
    std::unordered_map<int, PidCpuSnap> newSnap;

    DIR *dir = opendir("/proc");
    if (!dir) return;
    struct dirent *ent;
    while ((ent = readdir(dir))) {
        int pid;
        try { pid = std::stoi(ent->d_name); } catch (...) { continue; }

        std::ifstream sf(std::string("/proc/") + ent->d_name + "/stat");
        if (!sf.is_open()) continue;
        std::string line;
        std::getline(sf, line);
        auto lp = line.find('(');
        auto rp = line.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos) continue;

        std::string comm = line.substr(lp + 1, rp - lp - 1);
        std::istringstream rest(line.substr(rp + 2));
        std::string state;
        uint64_t dummy, utime, stime;
        rest >> state;
        for (int i = 0; i < 10; ++i) rest >> dummy;
        rest >> utime >> stime;

        std::string app = normalizeAppName(comm);
        PidCpuSnap s;
        s.appName = app;
        s.cpuTime = utime + stime;
        newSnap[pid] = s;

        auto it = prevPidSnap.find(pid);
        if (it != prevPidSnap.end()) {
            uint64_t procDelta = (utime + stime) - it->second.cpuTime;
            double share = static_cast<double>(procDelta) / sysDelta;
            if (share > 0) appCpuShare[app] += share;
        }
    }
    closedir(dir);

    prevPidSnap = std::move(newSnap);
    prevTotalCpu = totalCpuNow;

    // Compute total CPU share for normalization
    double totalShare = 0;
    for (auto &[app, share] : appCpuShare)
        totalShare += share;

    if (totalShare <= 0) return;

    double tickSeconds = 1.5; // Approximate refresh interval

    // Update per-app energy
    for (auto &[app, share] : appCpuShare) {
        double pct = (share / totalShare) * 100.0;
        double watts = (share / totalShare) * totalPower;
        double joules = watts * tickSeconds;

        appEnergy[app].currentWatts = watts;
        appEnergy[app].currentPercent = pct;
        appEnergy[app].totalJoules += joules;
    }

    // Zero out apps that didn't appear this cycle
    for (auto &[app, accum] : appEnergy) {
        if (appCpuShare.find(app) == appCpuShare.end()) {
            accum.currentWatts = 0;
            accum.currentPercent = 0;
        }
    }
}

std::vector<EnergyMonitor::AppEnergy> EnergyMonitor::perAppEnergy() const {
    std::vector<AppEnergy> result;
    for (auto &[app, accum] : appEnergy) {
        if (accum.totalJoules < 0.001 && accum.currentWatts < 0.001) continue;
        AppEnergy e;
        e.appName = app;
        e.watts = accum.currentWatts;
        e.percent = accum.currentPercent;
        e.energyJoules = accum.totalJoules;
        result.push_back(e);
    }
    std::sort(result.begin(), result.end(),
        [](const AppEnergy &a, const AppEnergy &b) { return a.watts > b.watts; });
    return result;
}

// ─── Constructor & Main Read ────────────────────────────────────

EnergyMonitor::EnergyMonitor() {
    detectCpu();
    initRapl();
    if (!raplAvailable) initAmdEnergy();
    readBattery();
}

void EnergyMonitor::read() {
    readRaplPower();
    readBattery();
    computePerAppEnergy();
}