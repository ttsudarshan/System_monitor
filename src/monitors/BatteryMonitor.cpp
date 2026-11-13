#include "BatteryMonitor.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {
bool readU64(const std::string &path, unsigned long long &out) {
    std::ifstream f(path);
    return static_cast<bool>(f >> out);
}
bool readLine(const std::string &path, std::string &out) {
    std::ifstream f(path);
    return static_cast<bool>(std::getline(f, out));
}
}

bool BatteryMonitor::findBattery() {
    try {
        for (auto &entry : fs::directory_iterator("/sys/class/power_supply")) {
            std::string type;
            if (readLine(entry.path().string() + "/type", type) && type == "Battery") {
                path_ = entry.path().string() + "/";
                return true;
            }
        }
    } catch (...) {}
    path_.clear();
    return false;
}

void BatteryMonitor::read() {
    if (path_.empty()) {
        // Probe at most once a minute: a battery can be hot-plugged, but
        // scanning sysfs every tick on a desktop is pointless.
        auto now = std::chrono::steady_clock::now();
        if (lastProbe_.time_since_epoch().count() != 0 && now - lastProbe_ < std::chrono::seconds(60)) {
            batteryPresent = false;
            return;
        }
        lastProbe_ = now;
        if (!findBattery()) { batteryPresent = false; return; }
    }

    unsigned long long v = 0;
    std::string s;

    if (!readLine(path_ + "status", s)) {
        // Battery vanished; probe again next time.
        path_.clear();
        batteryPresent = false;
        return;
    }
    batteryPresent = true;
    chargeStatus = s;

    if (readU64(path_ + "capacity", v)) level = static_cast<double>(v);

    if (readU64(path_ + "power_now", v)) {
        powerDraw = v / 1e6;
    } else {
        unsigned long long uV = 0, uA = 0;
        if (readU64(path_ + "voltage_now", uV) && readU64(path_ + "current_now", uA))
            powerDraw = (uV / 1e6) * (uA / 1e6);
    }

    if (readU64(path_ + "energy_now", v)) {
        energyNowWh = v / 1e6;
    } else {
        // Charge-based batteries report µAh; convert with the present voltage.
        unsigned long long uAh = 0, uV = 0;
        if (readU64(path_ + "charge_now", uAh) && readU64(path_ + "voltage_now", uV))
            energyNowWh = (uAh / 1e6) * (uV / 1e6);
    }
    if (readU64(path_ + "energy_full", v)) {
        energyFullWh = v / 1e6;
    } else {
        unsigned long long uAh = 0, uV = 0;
        if (readU64(path_ + "charge_full", uAh) && readU64(path_ + "voltage_now", uV))
            energyFullWh = (uAh / 1e6) * (uV / 1e6);
    }
}

int BatteryMonitor::minutesRemaining() const {
    if (!batteryPresent || !isDischarging() || powerDraw <= 0.1 || energyNowWh <= 0) return -1;
    return static_cast<int>(energyNowWh / powerDraw * 60.0);
}
