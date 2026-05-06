#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <chrono>
#include <QtSql/QSqlDatabase>

// Forward-declare Display to avoid pulling X11 headers into every TU
// that includes this header.
struct _XDisplay;
typedef struct _XDisplay Display;

class ScreenTimeTracker {
public:
    explicit ScreenTimeTracker(const std::string& dbPath = "", 
                              const std::string& procPath = "/proc");
    ~ScreenTimeTracker();

    // Call every refresh cycle (~1.5s)
    void tick();

    struct TabTime {
        std::string name;    // site/tab name extracted from window title
        int seconds;
    };

    struct AppScreenTime {
        std::string appName;
        int todaySeconds;
        int weekSeconds;
        std::vector<TabTime> todayTabs;   // per-site breakdown (browsers only)
        std::vector<TabTime> weeklyTabs;
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

    // Returns true only when the X screen saver has actually blanked/locked
    // the display — NOT merely because the user is idle (e.g. watching video).
    bool isScreenBlanked(Display *dpy) const;
    std::string getWaylandFocusedAppName();
    std::string getFocusedAppName();
    std::string normalizeAppName(const std::string &name);
    std::string getBrowserTabName(const std::string &app);
    void recordTick(const std::string &appName, const std::string &tabName, int seconds);
    
    std::string customDbPath;
    std::string procRoot;
};