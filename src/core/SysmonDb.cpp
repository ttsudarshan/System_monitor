#include "SysmonDb.h"
#include "AppNames.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

// ─── TimeUtil ───────────────────────────────────────────────────

std::string TimeUtil::dateString(int daysAgo) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    // Normalise to noon first so DST transitions cannot shift the date.
    t.tm_hour = 12; t.tm_min = 0; t.tm_sec = 0;
    t.tm_mday -= daysAgo;
    t.tm_isdst = -1;
    time_t shifted = mktime(&t);
    localtime_r(&shifted, &t);
    char buf[16];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
    return buf;
}

int TimeUtil::currentHour() {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    return t.tm_hour;
}

std::string TimeUtil::dayLabel(const std::string &date) {
    struct tm t{};
    if (sscanf(date.c_str(), "%d-%d-%d", &t.tm_year, &t.tm_mon, &t.tm_mday) != 3) return "";
    t.tm_year -= 1900; t.tm_mon -= 1; t.tm_hour = 12; t.tm_isdst = -1;
    time_t ts = mktime(&t);
    localtime_r(&ts, &t);
    char buf[8];
    strftime(buf, sizeof(buf), "%a", &t);
    return buf;
}

long long TimeUtil::nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ─── Paths ──────────────────────────────────────────────────────

static std::string homeDir() {
    const char *home = getenv("HOME");
    if (home && *home) return home;
    struct passwd *pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "/tmp";
}

std::string SysmonDb::defaultDir() { return homeDir() + "/.local/share/SystemMonitor"; }
std::string SysmonDb::defaultPath() { return defaultDir() + "/sysmon.db"; }
std::string SysmonDb::legacyPath() { return homeDir() + "/.local/share/System Monitor/sysmon.db"; }
std::string SysmonDb::pidFilePath() { return defaultDir() + "/tracker.pid"; }

// ─── Open / schema ──────────────────────────────────────────────

SysmonDb::SysmonDb(const std::string &path) : path_(path.empty() ? defaultPath() : path) {
    if (path.empty()) {
        std::string dir = defaultDir();
        mkdir((homeDir() + "/.local").c_str(), 0755);
        mkdir((homeDir() + "/.local/share").c_str(), 0755);
        mkdir(dir.c_str(), 0755);
    }
    if (sqlite3_open(path_.c_str(), &db_) != SQLITE_OK) {
        lastError_ = db_ ? sqlite3_errmsg(db_) : "sqlite3_open failed";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA journal_mode=WAL;");
    // WAL + NORMAL: no fsync per commit, data is still consistent after a crash.
    exec("PRAGMA synchronous=NORMAL;");
    if (!migrate()) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

SysmonDb::~SysmonDb() {
    if (db_) sqlite3_close(db_);
}

bool SysmonDb::exec(const std::string &sql) {
    if (!db_) return false;
    char *err = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        lastError_ = err ? err : "unknown error";
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool SysmonDb::hasTable(const std::string &table) const {
    return queryInt("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?", {table}) > 0;
}

bool SysmonDb::hasColumn(const std::string &table, const std::string &column) const {
    if (!db_) return false;
    sqlite3_stmt *s = nullptr;
    std::string sql = "PRAGMA table_info(" + table + ")";
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &s, nullptr) != SQLITE_OK) return false;
    bool found = false;
    while (sqlite3_step(s) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(s, 1);
        if (name && column == reinterpret_cast<const char *>(name)) { found = true; break; }
    }
    sqlite3_finalize(s);
    return found;
}

int SysmonDb::queryInt(const std::string &sql, const std::vector<std::string> &binds) const {
    if (!db_) return 0;
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &s, nullptr) != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(db_);
        return 0;
    }
    for (size_t i = 0; i < binds.size(); ++i)
        sqlite3_bind_text(s, static_cast<int>(i + 1), binds[i].c_str(), -1, SQLITE_TRANSIENT);
    int v = 0;
    if (sqlite3_step(s) == SQLITE_ROW) v = sqlite3_column_int(s, 0);
    sqlite3_finalize(s);
    return v;
}

bool SysmonDb::migrate() {
    static const char *const SCREEN_TIME_DDL =
        "CREATE TABLE %s ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  app_name TEXT NOT NULL,"
        "  tab_name TEXT NOT NULL DEFAULT '',"
        "  date TEXT NOT NULL,"
        "  hour INTEGER NOT NULL DEFAULT -1,"
        "  seconds INTEGER NOT NULL DEFAULT 0,"
        "  UNIQUE(app_name, tab_name, date, hour));";
    char ddl[1024];

    // BEGIN IMMEDIATE so a GUI and a daemon starting together serialise here.
    if (!exec("BEGIN IMMEDIATE;")) return false;

    bool ok = true;
    if (!hasTable("screen_time")) {
        snprintf(ddl, sizeof(ddl), SCREEN_TIME_DDL, "screen_time");
        ok = exec(ddl);
    } else if (!hasColumn("screen_time", "hour")) {
        // Older schema: (app_name[, tab_name], date, seconds). Rebuild with the
        // hour column; old rows keep hour = -1 (time of day unknown).
        bool hasTab = hasColumn("screen_time", "tab_name");
        snprintf(ddl, sizeof(ddl), SCREEN_TIME_DDL, "screen_time_new");
        ok = exec(ddl)
          && exec(std::string("INSERT INTO screen_time_new(app_name, tab_name, date, hour, seconds) ")
                  + "SELECT lower(app_name), " + (hasTab ? "COALESCE(tab_name,'')" : "''")
                  + ", date, -1, SUM(seconds) FROM screen_time GROUP BY 1, 2, 3;")
          && exec("DROP TABLE screen_time;")
          && exec("ALTER TABLE screen_time_new RENAME TO screen_time;");
    }

    ok = ok
      && exec("CREATE INDEX IF NOT EXISTS idx_st_date ON screen_time(date);")
      && exec("CREATE TABLE IF NOT EXISTS battery_drain ("
              "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
              "  app_name TEXT NOT NULL,"
              "  date TEXT NOT NULL,"
              "  watt_hours REAL NOT NULL DEFAULT 0,"
              "  UNIQUE(app_name, date));")
      && exec("CREATE INDEX IF NOT EXISTS idx_drain_date ON battery_drain(date);");

    // v3: earlier daemons fell back to a CPU heuristic on X11 sessions and
    // recorded tray applets, session daemons and even themselves as "apps".
    // Drop those rows once; the heuristic is now Wayland-only and filtered.
    if (ok && queryInt("PRAGMA user_version", {}) < 3) {
        sqlite3_stmt *s = nullptr;
        std::vector<std::string> noise;
        if (sqlite3_prepare_v2(db_, "SELECT DISTINCT app_name FROM screen_time", -1, &s, nullptr) == SQLITE_OK) {
            while (sqlite3_step(s) == SQLITE_ROW) {
                const unsigned char *n = sqlite3_column_text(s, 0);
                if (n && AppNames::isDesktopNoise(reinterpret_cast<const char *>(n)))
                    noise.emplace_back(reinterpret_cast<const char *>(n));
            }
            sqlite3_finalize(s);
        }
        for (const auto &app : noise) {
            sqlite3_stmt *d = nullptr;
            if (sqlite3_prepare_v2(db_, "DELETE FROM screen_time WHERE app_name = ?", -1, &d, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(d, 1, app.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(d);
                sqlite3_finalize(d);
            }
        }
    }

    // Rows written by older versions used several different name normalisers
    // ("msedge", "systemmonitor", "soffice.bin"...). Fold them into the
    // canonical keys so history and live data show up as one app. This runs
    // on every open: it is cheap (two DISTINCT scans) and also tidies up after
    // a daemon that was still running an older build.
    if (ok) {
        for (const char *table : {"screen_time", "battery_drain"}) {
            std::vector<std::pair<std::string, std::string>> renames;
            sqlite3_stmt *s = nullptr;
            std::string sql = std::string("SELECT DISTINCT app_name FROM ") + table;
            if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &s, nullptr) == SQLITE_OK) {
                while (sqlite3_step(s) == SQLITE_ROW) {
                    const unsigned char *n = sqlite3_column_text(s, 0);
                    if (!n) continue;
                    std::string old = reinterpret_cast<const char *>(n);
                    std::string canon = AppNames::canonical(old);
                    if (!canon.empty() && canon != old) renames.emplace_back(old, canon);
                }
                sqlite3_finalize(s);
            }
            for (const auto &[from, to] : renames)
                if (!renameApp(table, from, to)) { ok = false; break; }
            if (!ok) break;
        }
    }

    ok = ok && exec("PRAGMA user_version = 4;");

    if (!ok) {
        exec("ROLLBACK;");
        return false;
    }
    return exec("COMMIT;");
}

bool SysmonDb::renameApp(const std::string &table, const std::string &from, const std::string &to) {
    // Merge `from` into `to`, summing rows that collide on the unique key.
    std::string insert, conflict;
    if (table == "screen_time") {
        insert = "INSERT INTO screen_time(app_name, tab_name, date, hour, seconds) "
                 "SELECT ?1, tab_name, date, hour, seconds FROM screen_time WHERE app_name = ?2 "
                 "ON CONFLICT(app_name, tab_name, date, hour) DO UPDATE SET seconds = seconds + excluded.seconds";
    } else if (table == "battery_drain") {
        insert = "INSERT INTO battery_drain(app_name, date, watt_hours) "
                 "SELECT ?1, date, watt_hours FROM battery_drain WHERE app_name = ?2 "
                 "ON CONFLICT(app_name, date) DO UPDATE SET watt_hours = watt_hours + excluded.watt_hours";
    } else {
        return false;
    }
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_, insert.c_str(), -1, &s, nullptr) != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    sqlite3_bind_text(s, 1, to.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, from.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(s) == SQLITE_DONE;
    if (!ok) lastError_ = sqlite3_errmsg(db_);
    sqlite3_finalize(s);
    if (!ok) return false;

    std::string del = "DELETE FROM " + table + " WHERE app_name = ?1";
    if (sqlite3_prepare_v2(db_, del.c_str(), -1, &s, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(s, 1, from.c_str(), -1, SQLITE_TRANSIENT);
    ok = sqlite3_step(s) == SQLITE_DONE;
    sqlite3_finalize(s);
    return ok;
}

// ─── Screen time ────────────────────────────────────────────────

bool SysmonDb::recordScreenTime(const std::string &app, const std::string &tab,
                                const std::string &date, int hour, int seconds) {
    if (!db_ || app.empty() || seconds <= 0) return false;
    static const char *SQL =
        "INSERT INTO screen_time (app_name, tab_name, date, hour, seconds) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(app_name, tab_name, date, hour) DO UPDATE SET seconds = seconds + excluded.seconds;";
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    sqlite3_bind_text(s, 1, app.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, tab.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 3, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(s, 4, hour);
    sqlite3_bind_int(s, 5, seconds);
    bool ok = sqlite3_step(s) == SQLITE_DONE;
    if (!ok) lastError_ = sqlite3_errmsg(db_);
    sqlite3_finalize(s);
    return ok;
}

std::vector<AppTime> SysmonDb::appTotals(const std::string &from, const std::string &to) const {
    std::vector<AppTime> result;
    if (!db_) return result;

    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT app_name, SUM(seconds) FROM screen_time WHERE date BETWEEN ? AND ? "
            "GROUP BY app_name ORDER BY 2 DESC", -1, &s, nullptr) != SQLITE_OK)
        return result;
    sqlite3_bind_text(s, 1, from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, to.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(s) == SQLITE_ROW) {
        AppTime a;
        const unsigned char *n = sqlite3_column_text(s, 0);
        a.app = n ? reinterpret_cast<const char *>(n) : "";
        a.seconds = sqlite3_column_int(s, 1);
        result.push_back(std::move(a));
    }
    sqlite3_finalize(s);

    // Per-tab breakdown in a single query, folded into the app rows.
    if (sqlite3_prepare_v2(db_,
            "SELECT app_name, tab_name, SUM(seconds) FROM screen_time "
            "WHERE date BETWEEN ? AND ? AND tab_name != '' "
            "GROUP BY app_name, tab_name ORDER BY 3 DESC", -1, &s, nullptr) != SQLITE_OK)
        return result;
    sqlite3_bind_text(s, 1, from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, to.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(s) == SQLITE_ROW) {
        const unsigned char *an = sqlite3_column_text(s, 0);
        const unsigned char *tn = sqlite3_column_text(s, 1);
        std::string app = an ? reinterpret_cast<const char *>(an) : "";
        for (auto &a : result) {
            if (a.app == app) {
                a.tabs.push_back({tn ? reinterpret_cast<const char *>(tn) : "", sqlite3_column_int(s, 2)});
                break;
            }
        }
    }
    sqlite3_finalize(s);
    return result;
}

std::vector<DayTotal> SysmonDb::dailyTotals(int days) const {
    std::vector<DayTotal> result;
    for (int i = days - 1; i >= 0; --i) {
        DayTotal d;
        d.date = TimeUtil::dateString(i);
        d.label = (i == 0) ? "Today" : TimeUtil::dayLabel(d.date);
        d.seconds = queryInt("SELECT COALESCE(SUM(seconds),0) FROM screen_time WHERE date = ?", {d.date});
        result.push_back(std::move(d));
    }
    return result;
}

std::vector<int> SysmonDb::hourlyTotals(const std::string &date) const {
    std::vector<int> hours(24, 0);
    if (!db_) return hours;
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT hour, SUM(seconds) FROM screen_time WHERE date = ? AND hour BETWEEN 0 AND 23 "
            "GROUP BY hour", -1, &s, nullptr) != SQLITE_OK)
        return hours;
    sqlite3_bind_text(s, 1, date.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(s) == SQLITE_ROW) {
        int h = sqlite3_column_int(s, 0);
        if (h >= 0 && h < 24) hours[h] = sqlite3_column_int(s, 1);
    }
    sqlite3_finalize(s);
    return hours;
}

int SysmonDb::totalSeconds(const std::string &from, const std::string &to) const {
    return queryInt("SELECT COALESCE(SUM(seconds),0) FROM screen_time WHERE date BETWEEN ? AND ?", {from, to});
}

int SysmonDb::daysWithData(const std::string &from, const std::string &to) const {
    return queryInt("SELECT COUNT(DISTINCT date) FROM screen_time WHERE date BETWEEN ? AND ? AND seconds > 0",
                    {from, to});
}

// ─── Energy ─────────────────────────────────────────────────────

bool SysmonDb::addEnergy(const std::vector<std::pair<std::string, double>> &whByApp,
                         const std::string &date) {
    if (!db_ || whByApp.empty()) return false;
    static const char *SQL =
        "INSERT INTO battery_drain (app_name, date, watt_hours) VALUES (?, ?, ?) "
        "ON CONFLICT(app_name, date) DO UPDATE SET watt_hours = watt_hours + excluded.watt_hours;";
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_, SQL, -1, &s, nullptr) != SQLITE_OK) {
        lastError_ = sqlite3_errmsg(db_);
        return false;
    }
    bool ok = exec("BEGIN;");
    for (const auto &[app, wh] : whByApp) {
        if (app.empty() || wh <= 0) continue;
        sqlite3_reset(s);
        sqlite3_bind_text(s, 1, app.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(s, 3, wh);
        if (sqlite3_step(s) != SQLITE_DONE) { ok = false; lastError_ = sqlite3_errmsg(db_); }
    }
    sqlite3_finalize(s);
    return exec(ok ? "COMMIT;" : "ROLLBACK;") && ok;
}

std::vector<AppEnergyTotal> SysmonDb::energyTotals(const std::string &from, const std::string &to) const {
    std::vector<AppEnergyTotal> result;
    if (!db_) return result;
    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db_,
            "SELECT app_name, SUM(watt_hours) FROM battery_drain WHERE date BETWEEN ? AND ? "
            "GROUP BY app_name ORDER BY 2 DESC", -1, &s, nullptr) != SQLITE_OK)
        return result;
    sqlite3_bind_text(s, 1, from.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(s, 2, to.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(s) == SQLITE_ROW) {
        const unsigned char *n = sqlite3_column_text(s, 0);
        result.push_back({n ? reinterpret_cast<const char *>(n) : "", sqlite3_column_double(s, 1)});
    }
    sqlite3_finalize(s);
    return result;
}

// ─── Legacy import ──────────────────────────────────────────────

bool SysmonDb::importLegacy(const std::string &legacyPath) {
    if (!db_ || legacyPath == path_) return false;
    struct stat st;
    if (stat(legacyPath.c_str(), &st) != 0) return false;

    // ATTACH takes a string literal; escape single quotes.
    std::string escaped;
    for (char c : legacyPath) { if (c == '\'') escaped += '\''; escaped += c; }
    if (!exec("ATTACH DATABASE '" + escaped + "' AS legacy;")) return false;

    bool imported = false;
    bool ok = exec("BEGIN;");

    if (ok && queryInt("SELECT COUNT(*) FROM legacy.sqlite_master WHERE type='table' AND name='battery_drain'", {}) > 0) {
        ok = exec(
            "INSERT INTO battery_drain(app_name, date, watt_hours) "
            "SELECT * FROM (SELECT CASE WHEN app_name='chrome' THEN 'google-chrome' ELSE lower(app_name) END, "
            "       date, SUM(watt_hours) FROM legacy.battery_drain GROUP BY 1, 2) WHERE true "
            "ON CONFLICT(app_name, date) DO UPDATE SET watt_hours = watt_hours + excluded.watt_hours;");
        imported = imported || ok;
    }

    if (ok && queryInt("SELECT COUNT(*) FROM legacy.sqlite_master WHERE type='table' AND name='screen_time'", {}) > 0) {
        bool hasTab = false;
        sqlite3_stmt *s = nullptr;
        if (sqlite3_prepare_v2(db_, "PRAGMA legacy.table_info(screen_time)", -1, &s, nullptr) == SQLITE_OK) {
            while (sqlite3_step(s) == SQLITE_ROW) {
                const unsigned char *n = sqlite3_column_text(s, 1);
                if (n && std::string(reinterpret_cast<const char *>(n)) == "tab_name") hasTab = true;
            }
            sqlite3_finalize(s);
        }
        ok = exec(std::string(
            "INSERT INTO screen_time(app_name, tab_name, date, hour, seconds) "
            "SELECT * FROM (SELECT lower(app_name), ") + (hasTab ? "COALESCE(tab_name,'')" : "''") +
            ", date, -1, SUM(seconds) FROM legacy.screen_time GROUP BY 1, 2, 3) WHERE true "
            "ON CONFLICT(app_name, tab_name, date, hour) DO UPDATE SET seconds = seconds + excluded.seconds;");
        imported = imported || ok;
    }

    exec(ok ? "COMMIT;" : "ROLLBACK;");
    exec("DETACH DATABASE legacy;");

    if (ok) {
        std::rename(legacyPath.c_str(), (legacyPath + ".imported").c_str());
        // Leftover WAL/SHM files from the old connection are harmless but tidy them up.
        std::remove((legacyPath + "-wal").c_str());
        std::remove((legacyPath + "-shm").c_str());
    }
    return ok && imported;
}
