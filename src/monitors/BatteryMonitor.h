#pragma once

#include <chrono>
#include <string>

// Reads the first battery under /sys/class/power_supply (any device whose
// `type` is "Battery", not just BAT0).
class BatteryMonitor {
public:
    BatteryMonitor() = default;

    void read();

    bool hasBattery() const { return batteryPresent; }
    double percent() const { return level; }
    const std::string &status() const { return chargeStatus; }   // "Charging", "Discharging", "Full", ...
    bool isDischarging() const { return chargeStatus == "Discharging"; }
    double powerDrawWatts() const { return powerDraw; }
    double energyWh() const { return energyNowWh; }
    double capacityWh() const { return energyFullWh; }
    int minutesRemaining() const;  // -1 when not discharging / unknown

private:
    std::string path_;  // /sys/class/power_supply/<name>/
    std::chrono::steady_clock::time_point lastProbe_{};

    bool batteryPresent = false;
    double level = 0.0;
    std::string chargeStatus = "Unknown";
    double powerDraw = 0.0;
    double energyNowWh = 0.0;
    double energyFullWh = 0.0;

    bool findBattery();
};
