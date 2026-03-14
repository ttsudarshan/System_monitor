#pragma once

#include <string>
#include <cstdint>

class BatteryMonitor {
public:
    BatteryMonitor() = default;

    void read();

    bool hasBattery() const { return batteryPresent; }
    double percent() const { return level; }
    std::string status() const { return chargeStatus; }
    double powerDrawWatts() const { return powerDraw; }

private:
    bool batteryPresent = false;
    double level = 0.0;
    std::string chargeStatus = "Unknown";
    double powerDraw = 0.0;
};