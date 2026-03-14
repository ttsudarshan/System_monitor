#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <chrono>
#include <QtSql/QSqlDatabase>

class ScreenTimeTracker {
public:
    ScreenTimeTracker();
    ~ScreenTimeTracker();

    // Call every refresh cycle (~1.5s)
    void tick();

    struct AppScreenTime {
        std::string appName;
        int todaySeconds;
        int weekSeconds;
    };

    // Today's screen time per app, sorted descending
    std::vector<AppScreenTime> todayStats() const;

    // Weekly screen time per app
    std::vector<AppScreenTime> weeklyStats() const;

    // Per-day totals for the last 7 days (for bar chart)
    struct DailyTotal {
        std::string date;      // YYYY-MM-DD
        std::string dayLabel;  // "S", "M", "T", etc.
        int seconds;
    };
    std::vector<DailyTotal> last7Days() const;

    // Total screen time today
    int totalTodaySeconds() const;
    int totalWeekSeconds() const;
    int dailyAverageSeconds() const;

    // Currently focused app
    std::string currentApp() const { return currentFocusApp; }

private:
    QSqlDatabase db;
    std::string currentFocusApp;
    std::chrono::steady_clock::time_point lastTickTime;

    void initDb();
    std::string getFocusedAppName();
    std::string normalizeAppName(const std::string &name);
    void recordTick(const std::string &appName, int seconds);
};