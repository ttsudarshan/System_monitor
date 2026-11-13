#pragma once

#include <string>
#include <utility>
#include <vector>

struct sqlite3;

struct TabTime {
    std::string name;
    int seconds = 0;
};

struct AppTime {
    std::string app;        // canonical key (see AppNames)
    int seconds = 0;
    std::vector<TabTime> tabs;  // per-site breakdown, browsers only
};

struct DayTotal {
    std::string date;       // YYYY-MM-DD
    std::string label;      // "Mon" ... or "Today"
    int seconds = 0;
};

struct AppEnergyTotal {
    std::string app;
    double wattHours = 0;
};

namespace TimeUtil {
// Local date, `daysAgo` days before today, as YYYY-MM-DD.
std::string dateString(int daysAgo = 0);
int currentHour();                            // 0-23, local time
std::string dayLabel(const std::string &date);  // "Mon", "Tue", ...
long long nowMs();                            // steady clock, milliseconds
}

// Single SQLite database shared by the daemon and the GUI.
//
//   screen_time   (app_name, tab_name, date, hour, seconds)   hour = -1 for rows
//                                                              migrated from older
//                                                              versions
//   battery_drain (app_name, date, watt_hours)
//
// The connection is not thread-safe: use one SysmonDb per thread.
class SysmonDb {
public:
    static std::string defaultDir();     // ~/.local/share/SystemMonitor
    static std::string defaultPath();    // <defaultDir>/sysmon.db
    static std::string legacyPath();     // ~/.local/share/System Monitor/sysmon.db (old GUI)
    static std::string pidFilePath();    // <defaultDir>/tracker.pid

    explicit SysmonDb(const std::string &path = "");
    ~SysmonDb();
    SysmonDb(const SysmonDb &) = delete;
    SysmonDb &operator=(const SysmonDb &) = delete;

    bool isOpen() const { return db_ != nullptr; }
    const std::string &path() const { return path_; }
    std::string lastError() const { return lastError_; }

    // ── Screen time ────────────────────────────────────────────────
    bool recordScreenTime(const std::string &app, const std::string &tab,
                          const std::string &date, int hour, int seconds);

    // Per-app totals (with per-tab breakdown) for dates in [from, to].
    std::vector<AppTime> appTotals(const std::string &from, const std::string &to) const;
    // One entry per day for the last `days` days ending today, zero-filled.
    std::vector<DayTotal> dailyTotals(int days) const;
    // 24 entries for the given date. Rows with hour = -1 are not included.
    std::vector<int> hourlyTotals(const std::string &date) const;
    int totalSeconds(const std::string &from, const std::string &to) const;
    int daysWithData(const std::string &from, const std::string &to) const;

    // ── Energy ─────────────────────────────────────────────────────
    bool addEnergy(const std::vector<std::pair<std::string, double>> &whByApp,
                   const std::string &date);
    std::vector<AppEnergyTotal> energyTotals(const std::string &from, const std::string &to) const;

    // Merge the database an older version kept under "System Monitor/" (with a
    // space) into this one, then rename the old file so it is not imported twice.
    // Returns true if anything was imported.
    bool importLegacy(const std::string &legacyPath);

private:
    sqlite3 *db_ = nullptr;
    std::string path_;
    mutable std::string lastError_;

    bool migrate();
    bool renameApp(const std::string &table, const std::string &from, const std::string &to);
    bool exec(const std::string &sql);
    bool hasTable(const std::string &table) const;
    bool hasColumn(const std::string &table, const std::string &column) const;
    int queryInt(const std::string &sql, const std::vector<std::string> &binds) const;
};
