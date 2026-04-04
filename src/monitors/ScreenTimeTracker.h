#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
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
    ScreenTimeTracker();
    ~ScreenTimeTracker();

    // Call every refresh cycle (~1.5s)
    void tick();

    struct AppScreenTime {
        std::string appName;
        int todaySeconds;
        int weekSeconds;
    };

    struct BrowserTabTime {
        std::string browser;
        std::string site;
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

    // Browser tab stats: browser → [sites sorted by time desc]
    std::map<std::string, std::vector<BrowserTabTime>> browserTabTodayStats() const;
    std::map<std::string, std::vector<BrowserTabTime>> browserTabWeeklyStats() const;

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

    // Gets both the app name and window title from the focused window
    bool getFocusedAppAndTitle(std::string &appName, std::string &windowTitle);

    std::string normalizeAppName(const std::string &name);
    bool isBrowser(const std::string &appName) const;
    std::string extractSiteFromTitle(const std::string &title, const std::string &browser) const;
    void recordTick(const std::string &appName, int seconds);
    void recordBrowserTab(const std::string &browser, const std::string &site, int seconds);
};