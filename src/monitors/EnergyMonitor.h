#pragma once

#include "BatteryMonitor.h"
#include "core/ProcScan.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Hardware power readings (Intel/AMD RAPL, or the amd_energy hwmon driver)
// plus per-app attribution of that power by CPU-time share.
class EnergyMonitor {
public:
    EnergyMonitor();

    // `scan` is the /proc pass for this tick, `elapsedSec` the time since the previous read().
    void read(const ProcScanResult &scan, double elapsedSec);

    enum class CpuVendor { Unknown, Intel, AMD };
    CpuVendor vendor() const { return cpuVendor; }
    std::string vendorName() const;
    const std::string &cpuModelName() const { return modelName; }
    int coreCount() const { return numCores; }

    // RAPL readings (Watts)
    double packagePowerW() const { return packagePower; }
    double corePowerW() const { return corePower; }
    double dramPowerW() const { return dramPower; }
    bool hasHardwareEnergy() const { return raplAvailable; }

    const BatteryMonitor &battery() const { return battery_; }

    struct AppEnergy {
        std::string app;        // canonical name
        double watts = 0;       // current power attributed to this app
        double percent = 0;     // share of attributed power
        double energyJoules = 0;// accumulated since the GUI started
    };
    // Sorted by watts descending.
    std::vector<AppEnergy> perAppEnergy() const;

    // Which measurement the attribution is based on.
    enum class PowerSource { None, Rapl, Battery };
    PowerSource powerSource() const { return powerSource_; }
    double attributedPowerW() const { return attributedPower_; }

    // Energy accumulated per app since the last call, in Wh, for persisting.
    std::vector<std::pair<std::string, double>> takePendingWh();

private:
    CpuVendor cpuVendor = CpuVendor::Unknown;
    std::string modelName;
    int numCores = 0;
    void detectCpu();

    // RAPL
    bool raplAvailable = false;
    std::string packagePath_, corePath_, dramPath_;
    uint64_t raplMaxEnergy = 0;
    uint64_t prevPackageEnergy = 0, prevCoreEnergy = 0, prevDramEnergy = 0;
    uint64_t prevTimestampUs = 0;
    double packagePower = 0, corePower = 0, dramPower = 0;
    void initRapl();
    void initAmdEnergy();
    void readRaplPower();
    static bool readCounter(const std::string &path, uint64_t &out);
    static uint64_t nowUs();

    BatteryMonitor battery_;

    // Attribution
    PowerSource powerSource_ = PowerSource::None;
    double attributedPower_ = 0;
    struct AppAccum {
        double totalJoules = 0;
        double pendingWh = 0;
        double currentWatts = 0;
        double currentPercent = 0;
    };
    std::unordered_map<std::string, AppAccum> appEnergy;
    void attribute(const ProcScanResult &scan, double elapsedSec);
};
