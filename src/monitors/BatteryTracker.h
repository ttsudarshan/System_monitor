#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <QtSql/QSqlDatabase>

class BatteryTracker {
public:
    BatteryTracker();
    ~BatteryTracker();

    // Call every refresh cycle
    void tick();

    // Grouped by application name (brave, code, etc.)
    struct AppDrain {
        std::string appName;
        double drainPercent;      // % share of current power draw
        double totalWhToday;      // Watt-hours consumed today
        double totalWhWeek;       // Watt-hours consumed this week
    };

    // Current live drain per app
    std::vector<AppDrain> currentDrain() const;

    // Historical daily stats
    struct DailyAppDrain {
        std::string appName;
        std::string date;   // YYYY-MM-DD
        double totalWh;
    };
    std::vector<DailyAppDrain> dailyStats(const std::string &date) const;
    std::vector<DailyAppDrain> weeklyStats() const; // last 7 days aggregated per app

    bool hasBattery() const { return batteryPresent; }

private:
    QSqlDatabase db;
    bool batteryPresent = false;

    // Per-PID CPU tracking for grouping
    struct PidCpuData {
        std::string appName;
        uint64_t utime = 0;
        uint64_t stime = 0;
    };
    std::unordered_map<int, PidCpuData> prevPidData;
    uint64_t prevTotalCpu = 0;

    // Live drain map: appName -> % of power
    std::unordered_map<std::string, double> liveDrain;

    void initDb();
    double readSystemPowerW();
    uint64_t readTotalCpuTime();

    // Group PIDs by app name (e.g. all brave PIDs -> "brave")
    std::string normalizeAppName(const std::string &comm);
};