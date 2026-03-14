#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

class EnergyMonitor {
public:
    EnergyMonitor();

    // Call every refresh cycle
    void read();

    enum class CpuVendor { Unknown, Intel, AMD };

    CpuVendor vendor() const { return cpuVendor; }
    std::string vendorName() const;
    std::string cpuModelName() const { return modelName; }
    int coreCount() const { return numCores; }

    // RAPL / AMD Energy readings (Watts)
    double packagePowerW() const { return packagePower; }
    double corePowerW() const { return corePower; }
    double dramPowerW() const { return dramPower; }
    double totalMeasuredW() const { return packagePower; }

    // Is hardware energy monitoring available?
    bool hasHardwareEnergy() const { return raplAvailable; }

    // Per-app energy attribution
    struct AppEnergy {
        std::string appName;
        double watts;          // Current power draw estimate
        double percent;        // % of total measured power
        double energyJoules;   // Accumulated energy this session
    };

    // Get per-app energy sorted by watts descending
    std::vector<AppEnergy> perAppEnergy() const;

    // Battery info
    bool hasBattery() const { return batteryPresent; }
    double batteryPercent() const { return battLevel; }
    std::string batteryStatus() const { return battStatus; }
    double batteryPowerW() const { return battPower; }
    double batteryEnergyWh() const { return battEnergyWh; }
    double batteryCapacityWh() const { return battCapacityWh; }
    int estimatedMinutesRemaining() const;

private:
    // CPU detection
    CpuVendor cpuVendor = CpuVendor::Unknown;
    std::string modelName;
    int numCores = 0;
    void detectCpu();

    // RAPL
    bool raplAvailable = false;
    std::string raplBasePath;
    double raplMaxEnergy = 0;       // Max energy counter value (microjoules)

    uint64_t prevPackageEnergy = 0;
    uint64_t prevCoreEnergy = 0;
    uint64_t prevDramEnergy = 0;
    uint64_t prevTimestamp = 0;

    double packagePower = 0;
    double corePower = 0;
    double dramPower = 0;

    void initRapl();
    uint64_t readRaplEnergy(const std::string &domain);
    void readRaplPower();

    // AMD Energy (uses same RAPL interface on modern kernels)
    void initAmdEnergy();

    // Per-process CPU tracking for energy attribution
    struct PidCpuSnap {
        std::string appName;
        uint64_t cpuTime = 0;  // utime + stime
    };
    std::unordered_map<int, PidCpuSnap> prevPidSnap;
    uint64_t prevTotalCpu = 0;

    // Accumulated per-app energy
    struct AppAccum {
        double totalJoules = 0;
        double currentWatts = 0;
        double currentPercent = 0;
    };
    std::unordered_map<std::string, AppAccum> appEnergy;

    void computePerAppEnergy();
    uint64_t readTotalCpuTime();
    static std::string normalizeAppName(const std::string &comm);

    // Battery
    bool batteryPresent = false;
    double battLevel = 0;
    std::string battStatus = "Unknown";
    double battPower = 0;
    double battEnergyWh = 0;
    double battCapacityWh = 0;
    void readBattery();

    // Timing
    uint64_t nowUs();
};