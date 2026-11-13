#pragma once

#include "core/FocusDetector.h"
#include "core/SysmonDb.h"

#include <string>
#include <vector>

struct ScreenTimeStats {
    std::vector<AppTime> today;     // sorted by seconds desc
    std::vector<AppTime> week;      // last 7 days including today
    std::vector<DayTotal> last7;    // oldest first, last entry is today
    std::vector<int> hourly;        // 24 buckets for today
    int todaySeconds = 0;
    int weekSeconds = 0;
    int dailyAverageSeconds = 0;    // over days in the last 7 that have data
    std::string currentApp;         // canonical, "" when nothing/screen off
    std::string currentTab;
    bool screenOff = false;
    bool daemonRunning = false;
    std::string source;             // "X11", "GNOME extension", ...
};

// GUI-side screen time: reads statistics from the shared database and, only
// when the background daemon is not running, records time itself.
class ScreenTimeTracker {
public:
    ScreenTimeTracker(SysmonDb &db, FocusDetector &detector);

    void tick(long long elapsedMs);
    ScreenTimeStats stats() const;

    bool daemonRunning() const { return daemonRunning_; }
    void flush();

private:
    SysmonDb &db_;
    FocusDetector &detector_;

    FocusDetector::Result current_;
    bool daemonRunning_ = false;
    long long lastDaemonCheckMs_ = 0;

    // Fallback accumulator (mirrors the daemon's)
    std::string accApp_, accTab_, accDate_;
    int accHour_ = -1;
    long long accMs_ = 0;
    long long lastFlushMs_ = 0;

    bool checkDaemon();
};
