#include "BatteryMonitor.h"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

void BatteryMonitor::read() {
    batteryPresent = false;
    std::string basePath = "/sys/class/power_supply/";

    try {
        for (auto &entry : fs::directory_iterator(basePath)) {
            std::string name = entry.path().filename().string();
            // Look for BAT0, BAT1, etc.
            if (name.find("BAT") != 0) continue;

            batteryPresent = true;
            std::string path = entry.path().string() + "/";

            // Read capacity (percentage)
            std::ifstream capF(path + "capacity");
            if (capF.is_open()) {
                capF >> level;
            }

            // Read status
            std::ifstream statF(path + "status");
            if (statF.is_open()) {
                std::getline(statF, chargeStatus);
            }

            // Read power draw
            // Try power_now first (in microwatts)
            std::ifstream powerF(path + "power_now");
            if (powerF.is_open()) {
                uint64_t microWatts;
                powerF >> microWatts;
                powerDraw = microWatts / 1000000.0;
            } else {
                // Fallback: voltage_now * current_now
                std::ifstream voltF(path + "voltage_now");
                std::ifstream currF(path + "current_now");
                if (voltF.is_open() && currF.is_open()) {
                    uint64_t microVolts, microAmps;
                    voltF >> microVolts;
                    currF >> microAmps;
                    powerDraw = (microVolts / 1000000.0) * (microAmps / 1000000.0);
                }
            }

            break; // Use first battery found
        }
    } catch (...) {
        batteryPresent = false;
    }
}