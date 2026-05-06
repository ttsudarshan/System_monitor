#include "ScreenTimeTracker.h"
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDate>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/scrnsaver.h>
// Undef X11 macros that clash with Qt
#undef Bool
#undef True
#undef False
#undef None
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <vector>

// ---------------------------------------------------------------------------
// FIX: Mobile-style tracking — count time whenever the screen is ON,
//      regardless of mouse/keyboard idle time.
//      Only stop counting when the screen is actually blanked or locked.
//
// XScreenSaver states:
//   ScreenSaverOff      (0) — screen saver not active, display on  ← TRACK
//   ScreenSaverOn       (1) — screen blanked / locked              ← STOP
//   ScreenSaverCycle    (2) — cycling patterns                     ← STOP
//   ScreenSaverDisabled (3) — no screen saver installed            ← TRACK
// ---------------------------------------------------------------------------

ScreenTimeTracker::ScreenTimeTracker(const std::string& dbP, const std::string& pRoot) 
    : customDbPath(dbP), procRoot(pRoot) {
    initDb();
    lastTickTime = std::chrono::steady_clock::now();
}

ScreenTimeTracker::~ScreenTimeTracker() {
    if (db.isOpen()) db.close();
}

void ScreenTimeTracker::initDb() {
    QString dbPath;
    if (customDbPath.empty()) {
        QString home = QDir::homePath();
        QString dataDir = home + "/.local/share/SystemMonitor";
        QDir().mkpath(dataDir);
        dbPath = dataDir + "/sysmon.db";
    } else {
        dbPath = QString::fromStdString(customDbPath);
    }

    db = QSqlDatabase::addDatabase("QSQLITE", "screen_time_tracker_" + QString::number(reinterpret_cast<quintptr>(this)));
    db.setDatabaseName(dbPath);
    db.open();

    QSqlQuery q(db);
    q.exec("PRAGMA journal_mode=WAL");

    // Detect table state for migration
    bool tableExists = false;
    bool hasTabName  = false;

    if (q.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='screen_time'"))
        if (q.next()) tableExists = true;

    if (tableExists) {
        if (q.exec("PRAGMA table_info(screen_time)")) {
            while (q.next()) {
                if (q.value(1).toString() == "tab_name") { hasTabName = true; break; }
            }
        }
    }

    if (!tableExists) {
        q.exec("CREATE TABLE screen_time ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "app_name TEXT NOT NULL,"
               "tab_name TEXT NOT NULL DEFAULT '',"
               "date TEXT NOT NULL,"
               "seconds INTEGER NOT NULL DEFAULT 0,"
               "UNIQUE(app_name, tab_name, date))");
        q.exec("CREATE INDEX IF NOT EXISTS idx_st_date ON screen_time(date)");
    } else if (!hasTabName) {
        // Migrate existing rows: add tab_name column and update UNIQUE constraint
        q.exec("BEGIN");
        q.exec("CREATE TABLE screen_time_new ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "app_name TEXT NOT NULL,"
               "tab_name TEXT NOT NULL DEFAULT '',"
               "date TEXT NOT NULL,"
               "seconds INTEGER NOT NULL DEFAULT 0,"
               "UNIQUE(app_name, tab_name, date))");
        q.exec("INSERT INTO screen_time_new(app_name, tab_name, date, seconds) "
               "SELECT app_name, '', date, seconds FROM screen_time");
        q.exec("DROP TABLE screen_time");
        q.exec("ALTER TABLE screen_time_new RENAME TO screen_time");
        q.exec("COMMIT");
        q.exec("CREATE INDEX IF NOT EXISTS idx_st_date ON screen_time(date)");
    }
}

std::string ScreenTimeTracker::normalizeAppName(const std::string &name) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);

    if (n.find("brave") != std::string::npos) return "brave";
    if (n.find("firefox") != std::string::npos) return "firefox";
    if (n.find("chrome") != std::string::npos) return "google-chrome";
    if (n.find("chromium") != std::string::npos) return "chromium";
    if (n == "code" || n.find("code") == 0) return "vscode";
    if (n.find("slack") != std::string::npos) return "slack";
    if (n.find("discord") != std::string::npos) return "discord";
    if (n.find("spotify") != std::string::npos) return "spotify";
    if (n.find("nemo") != std::string::npos) return "nemo";
    if (n.find("terminal") != std::string::npos || n.find("gnome-terminal") != std::string::npos
        || n.find("xterm") != std::string::npos || n.find("konsole") != std::string::npos
        || n.find("alacritty") != std::string::npos || n.find("kitty") != std::string::npos
        || n.find("tilix") != std::string::npos)
        return "terminal";
    if (n.find("libreoffice") != std::string::npos) return "libreoffice";
    if (n.find("gimp") != std::string::npos) return "gimp";
    if (n.find("vlc") != std::string::npos) return "vlc";
    if (n.find("telegram") != std::string::npos) return "telegram";
    if (n.find("thunderbird") != std::string::npos) return "thunderbird";
    if (n.find("microsoft-edge") != std::string::npos || n.find("msedge") != std::string::npos) return "microsoft-edge";
    if (n.find("systemmonitor") != std::string::npos) return "system monitor";
    if (n.find("gnome-system") != std::string::npos) return "gnome-system-monitor";
    if (n.find("gnome-screenshot") != std::string::npos) return "gnome-screenshot";
    if (n.find("zoom") != std::string::npos) return "zoom";
    if (n.find("teams") != std::string::npos) return "teams";
    if (n.find("obs") != std::string::npos) return "obs-studio";

    return n;
}

// ---------------------------------------------------------------------------
// Browser tab name extraction
// Reads _NET_WM_NAME from the active X11/XWayland window and returns the
// site name stripped of the browser suffix.  Returns "" for non-browsers.
// ---------------------------------------------------------------------------
static bool isBrowserApp(const std::string &app) {
    return app == "brave" || app == "firefox" || app == "google-chrome"
        || app == "chromium" || app == "microsoft-edge";
}

std::string ScreenTimeTracker::getBrowserTabName(const std::string &app) {
    if (!isBrowserApp(app)) return "";

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return "";

    std::string title;
    Window activeWin = 0;

    // Get active window ID
    Atom netActive = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", 0);
    {
        Atom at; int af; unsigned long n, ba; unsigned char *p = nullptr;
        if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), netActive,
                               0, 1, 0, XA_WINDOW, &at, &af, &n, &ba, &p) == Success && p) {
            activeWin = *reinterpret_cast<Window*>(p);
            XFree(p);
        }
    }

    if (activeWin > 1) {
        // Try _NET_WM_NAME (UTF-8)
        Atom netWmName = XInternAtom(dpy, "_NET_WM_NAME", 0);
        Atom utf8Type  = XInternAtom(dpy, "UTF8_STRING",  0);
        Atom at; int af; unsigned long n, ba; unsigned char *p = nullptr;

        if (XGetWindowProperty(dpy, activeWin, netWmName, 0, 2048, 0, utf8Type,
                               &at, &af, &n, &ba, &p) == Success && p && n > 0) {
            title = reinterpret_cast<char*>(p);
            XFree(p);
        } else {
            if (p) XFree(p);
            // Fall back to WM_NAME
            Atom wmName = XInternAtom(dpy, "WM_NAME", 0);
            if (XGetWindowProperty(dpy, activeWin, wmName, 0, 2048, 0, AnyPropertyType,
                                   &at, &af, &n, &ba, &p) == Success && p && n > 0) {
                title = reinterpret_cast<char*>(p);
                XFree(p);
            } else if (p) XFree(p);
        }
    }
    XCloseDisplay(dpy);

    if (title.empty()) return "";

    // Strip browser name suffix
    static const char* const SUFFIXES[] = {
        " \xe2\x80\x94 Mozilla Firefox",
        " - Mozilla Firefox",
        " \xe2\x80\x94 Firefox Nightly",
        " - Firefox Nightly",
        " - Google Chrome",
        " - Chromium",
        " - Microsoft Edge",
        " \xe2\x80\x94 Brave",
        " - Brave",
        " - Firefox",
        nullptr
    };
    for (int i = 0; SUFFIXES[i]; ++i) {
        const std::string sfx(SUFFIXES[i]);
        if (title.size() >= sfx.size()
                && title.compare(title.size() - sfx.size(), sfx.size(), sfx) == 0) {
            title.resize(title.size() - sfx.size());
            break;
        }
    }
    while (!title.empty() && (unsigned char)title.back() <= ' ') title.pop_back();
    if (title.empty()) return "";

    // Extract the rightmost short segment as site name
    auto lastShortSeg = [&](const std::string &sep) -> std::string {
        size_t pos = title.rfind(sep);
        if (pos == std::string::npos) return "";
        std::string seg = title.substr(pos + sep.size());
        while (!seg.empty() && (unsigned char)seg.front() <= ' ') seg.erase(seg.begin());
        while (!seg.empty() && (unsigned char)seg.back()  <= ' ') seg.pop_back();
        return (seg.size() > 0 && seg.size() <= 35) ? seg : "";
    };

    std::string site;
    site = lastShortSeg(" \xc2\xb7 ");   // " · "  U+00B7 (GitHub, Wikipedia)
    if (site.empty()) site = lastShortSeg(" | ");
    if (site.empty()) site = lastShortSeg(" \xe2\x80\x94 ");  // " — "  U+2014
    if (site.empty()) site = lastShortSeg(" - ");
    if (!site.empty()) return site;

    // No separator — the whole remaining title is the site name (e.g. "YouTube")
    if (title.size() > 50) return title.substr(0, 47) + "...";
    return title;
}

// ---------------------------------------------------------------------------
// Wayland focus detection helper
// Uses GNOME Shell JS eval to query the compositor directly — works on
// GNOME Wayland (Mutter) without any third-party extension.
// ---------------------------------------------------------------------------
std::string ScreenTimeTracker::getWaylandFocusedAppName() {
    // Call the custom GNOME extension we installed
    FILE *fp = popen(
        "gdbus call --session "
        "--dest org.sysmon.Tracker "
        "--object-path /org/sysmon/Tracker "
        "--method org.sysmon.Tracker.GetActiveApp "
        "2>/dev/null",
        "r");
    if (!fp) return "";

    char buf[1024];
    std::string out;
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);

    // Filter output: it should be something like ('ClassName',) or (true, 'ClassName')
    // depending on the exact glue code. The extension uses (s) -> (true, 'ClassName')
    // usually in gdbus output for tuples.
    size_t start = out.find("'");
    if (start != std::string::npos) {
        size_t end = out.find("'", start + 1);
        if (end != std::string::npos && end > start + 1) {
            std::string cls = out.substr(start + 1, end - start - 1);
            if (!cls.empty())
                return normalizeAppName(cls);
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// isScreenBlanked()
//
// Returns true only when the X screen saver has actually blanked or locked
// the display. A user sitting still watching a video returns false — the
// screen is still ON, just idle from an input perspective.
// ---------------------------------------------------------------------------
bool ScreenTimeTracker::isScreenBlanked(Display *dpy) const {
    // 1. Try GNOME native Wayland detection first
    FILE *fp = popen("busctl --user call org.gnome.Mutter.IdleMonitor /org/gnome/Mutter/IdleMonitor/Core org.gnome.Mutter.IdleMonitor GetIdletime 2>/dev/null", "r");
    if (fp) {
        char buf[256];
        if (fgets(buf, sizeof(buf), fp)) {
            // Check if screen is locked
            FILE *lfp = popen("gdbus call --session --dest org.gnome.ScreenSaver --object-path /org/gnome/ScreenSaver --method org.gnome.ScreenSaver.GetActive 2>/dev/null", "r");
            if (lfp) {
                char lbuf[64];
                if (fgets(lbuf, sizeof(lbuf), lfp)) {
                    if (strstr(lbuf, "true")) {
                        pclose(lfp);
                        pclose(fp);
                        return true;
                    }
                }
                pclose(lfp);
            }
        }
        pclose(fp);
    }

    // 2. Fall back to X11
    if (!dpy) return false;
    int evBase, errBase;
    if (!XScreenSaverQueryExtension(dpy, &evBase, &errBase))
        return false; // Extension not present → assume screen is on

    XScreenSaverInfo *info = XScreenSaverAllocInfo();
    if (!info) return false;

    XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), info);
    int state = info->state;
    XFree(info);

    // ScreenSaverOn (1) or ScreenSaverCycle (2) → display truly blanked
    return (state == ScreenSaverOn || state == ScreenSaverCycle);
}

std::string ScreenTimeTracker::getFocusedAppName() {
    // 1. Tier 1: GNOME Shell Extension (Most accurate if loaded)
    std::string wayApp = getWaylandFocusedAppName();
    if (!wayApp.empty()) return wayApp;

    // 2. Tier 2: X11 / XWayland via _NET_ACTIVE_WINDOW
    // This catches many common apps (VSCode, Chrome, Discord, Slack) even on Wayland.
    Display *dpy = XOpenDisplay(nullptr);
    if (dpy) {
        if (!isScreenBlanked(dpy)) {
            Window activeWin = 0;
            Atom netActiveAtom = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", 0);
            if (netActiveAtom != 0) {
                Atom actualType;
                int actualFormat;
                unsigned long nItems, bytesAfter;
                unsigned char *prop = nullptr;
                if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), netActiveAtom,
                                       0, 1, 0, XA_WINDOW,
                                       &actualType, &actualFormat,
                                       &nItems, &bytesAfter, &prop) == Success && prop) {
                    activeWin = *reinterpret_cast<Window*>(prop);
                    XFree(prop);
                }
            }

            if (activeWin == 0) {
                int revert;
                XGetInputFocus(dpy, &activeWin, &revert);
            }

            if (activeWin > 1) {
                Atom wmClass = XInternAtom(dpy, "WM_CLASS", 0);
                Window current = activeWin;
                Window root = DefaultRootWindow(dpy);
                Window parent;
                Window *children;
                unsigned int nChildren;

                for (int i = 0; i < 20; ++i) {
                    Atom actualType;
                    int actualFormat;
                    unsigned long nItems, bytesAfter;
                    unsigned char *prop = nullptr;

                    if (XGetWindowProperty(dpy, current, wmClass, 0, 1024, 0,
                                           XA_STRING, &actualType, &actualFormat,
                                           &nItems, &bytesAfter, &prop) == Success
                        && prop && nItems > 0) {
                        std::string instance(reinterpret_cast<char*>(prop));
                        std::string className;
                        if (nItems > instance.length() + 1)
                            className = std::string(reinterpret_cast<char*>(prop) + instance.length() + 1);
                        XFree(prop);
                        XCloseDisplay(dpy);
                        return normalizeAppName(className.empty() ? instance : className);
                    }
                    if (prop) XFree(prop);

                    if (!XQueryTree(dpy, current, &root, &parent, &children, &nChildren)) break;
                    if (children) XFree(children);
                    if (parent == root || parent == current) break;
                    current = parent;
                }
            }
        }
        XCloseDisplay(dpy);
    }

    // 3. Tier 3: Wayland Heuristic (Fallback for native apps)
    static std::unordered_map<int, unsigned long long> lastCpuTimes;
    static std::string cachedHeuristicApp;
    static std::chrono::steady_clock::time_point lastHeuristicTime;
    
    auto now = std::chrono::steady_clock::now();
    bool shouldScan = std::chrono::duration_cast<std::chrono::seconds>(now - lastHeuristicTime).count() >= 2;

    if (!shouldScan) return cachedHeuristicApp;

    int winnerPid = -1;
    unsigned long long maxDelta = 0;

    // Candidates: Apps with libwayland-client or libxcb loaded
    FILE *fp = popen("grep -l \"libwayland-client\\|libxcb\" /proc/*/maps 2>/dev/null | cut -d'/' -f3", "r");
    if (fp) {
        char buf[64];
        while (fgets(buf, sizeof(buf), fp)) {
            int pid = atoi(buf);
            if (pid <= 100) continue;

            std::string statFile = procRoot + "/" + std::to_string(pid) + "/stat";
            std::ifstream s(statFile);
            if (!s.is_open()) continue;
            std::string tmp;
            for (int i = 0; i < 13; ++i) s >> tmp;
            unsigned long long utime, stime;
            if (s >> utime >> stime) {
                unsigned long long total = utime + stime;
                if (lastCpuTimes.count(pid)) {
                    unsigned long long delta = (total >= lastCpuTimes[pid]) ? total - lastCpuTimes[pid] : 0;
                    if (delta > maxDelta) {
                        maxDelta = delta;
                        winnerPid = pid;
                    }
                }
                lastCpuTimes[pid] = total;
            }
        }
        pclose(fp);
    }

    lastHeuristicTime = now;
    if (winnerPid > 0 && maxDelta > 0) {
        std::string commFile = procRoot + "/" + std::to_string(winnerPid) + "/comm";
        std::ifstream c(commFile);
        std::string name;
        if (std::getline(c, name) && !name.empty()) {
            if (name != "gnome-shell" && name != "mutter") {
                cachedHeuristicApp = normalizeAppName(name);
                return cachedHeuristicApp;
            }
        }
    }
    cachedHeuristicApp.clear();
    return "";
}

void ScreenTimeTracker::tick() {
    auto now = std::chrono::steady_clock::now();
    int elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTickTime).count();
    lastTickTime = now;

    // Clamp elapsed to reasonable range (handles app being paused/resumed)
    if (elapsed <= 0) elapsed = 1;
    if (elapsed > 10) elapsed = 2;

    std::string app = getFocusedAppName();
    currentFocusApp = app;
    if (app.empty()) return;

    // Check if daemon is active — if so, it owns the writes; GUI only reads
    bool daemonRunning = false;
    FILE *fp_check = popen("pgrep -x sysmon-tracker 2>/dev/null", "r");
    if (fp_check) {
        char buf[32];
        if (fgets(buf, sizeof(buf), fp_check)) daemonRunning = true;
        pclose(fp_check);
    }
    if (daemonRunning) return;

    std::string tab = getBrowserTabName(app);
    recordTick(app, tab, elapsed);
}

void ScreenTimeTracker::recordTick(const std::string &appName, const std::string &tabName, int seconds) {
    std::string today = QDate::currentDate().toString("yyyy-MM-dd").toStdString();

    QSqlQuery q(db);
    q.prepare("INSERT INTO screen_time (app_name, tab_name, date, seconds) "
              "VALUES (:app, :tab, :date, :sec) "
              "ON CONFLICT(app_name, tab_name, date) DO UPDATE SET seconds = seconds + :sec2");
    q.bindValue(":app", QString::fromStdString(appName));
    q.bindValue(":tab", QString::fromStdString(tabName));
    q.bindValue(":date", QString::fromStdString(today));
    q.bindValue(":sec", seconds);
    q.bindValue(":sec2", seconds);
    q.exec();
}

std::vector<ScreenTimeTracker::AppScreenTime> ScreenTimeTracker::todayStats() const {
    std::vector<AppScreenTime> result;
    QString today   = QDate::currentDate().toString("yyyy-MM-dd");
    QString weekAgo = QDate::currentDate().addDays(-7).toString("yyyy-MM-dd");

    // Per-app totals (sum across all tab_names)
    QSqlQuery q(db);
    q.prepare("SELECT app_name, SUM(seconds) FROM screen_time "
              "WHERE date = :d GROUP BY app_name ORDER BY SUM(seconds) DESC");
    q.bindValue(":d", today);
    if (!q.exec()) return result;

    while (q.next()) {
        AppScreenTime s;
        s.appName      = q.value(0).toString().toStdString();
        s.todaySeconds = q.value(1).toInt();

        // Weekly total for this app
        QSqlQuery wq(db);
        wq.prepare("SELECT SUM(seconds) FROM screen_time WHERE app_name=:a AND date>=:d");
        wq.bindValue(":a", q.value(0).toString());
        wq.bindValue(":d", weekAgo);
        s.weekSeconds = 0;
        if (wq.exec() && wq.next()) s.weekSeconds = wq.value(0).toInt();

        // Per-tab breakdown (browsers only — rows with non-empty tab_name)
        QSqlQuery tq(db);
        tq.prepare("SELECT tab_name, SUM(seconds) FROM screen_time "
                   "WHERE date=:d AND app_name=:a AND tab_name!='' "
                   "GROUP BY tab_name ORDER BY SUM(seconds) DESC");
        tq.bindValue(":d", today);
        tq.bindValue(":a", q.value(0).toString());
        if (tq.exec()) {
            while (tq.next())
                s.todayTabs.push_back({tq.value(0).toString().toStdString(), tq.value(1).toInt()});
        }

        result.push_back(s);
    }
    return result;
}

std::vector<ScreenTimeTracker::AppScreenTime> ScreenTimeTracker::weeklyStats() const {
    std::vector<AppScreenTime> result;
    QString weekAgo = QDate::currentDate().addDays(-7).toString("yyyy-MM-dd");

    QSqlQuery q(db);
    q.prepare("SELECT app_name, SUM(seconds) FROM screen_time "
              "WHERE date>=:d GROUP BY app_name ORDER BY SUM(seconds) DESC");
    q.bindValue(":d", weekAgo);
    if (!q.exec()) return result;

    while (q.next()) {
        AppScreenTime s;
        s.appName     = q.value(0).toString().toStdString();
        s.weekSeconds = q.value(1).toInt();
        s.todaySeconds = 0;

        // Per-tab breakdown for the week
        QSqlQuery tq(db);
        tq.prepare("SELECT tab_name, SUM(seconds) FROM screen_time "
                   "WHERE date>=:d AND app_name=:a AND tab_name!='' "
                   "GROUP BY tab_name ORDER BY SUM(seconds) DESC");
        tq.bindValue(":d", weekAgo);
        tq.bindValue(":a", q.value(0).toString());
        if (tq.exec()) {
            while (tq.next())
                s.weeklyTabs.push_back({tq.value(0).toString().toStdString(), tq.value(1).toInt()});
        }

        result.push_back(s);
    }
    return result;
}

int ScreenTimeTracker::totalTodaySeconds() const {
    std::string today = QDate::currentDate().toString("yyyy-MM-dd").toStdString();
    QSqlQuery q(db);
    q.prepare("SELECT SUM(seconds) FROM screen_time WHERE date = :d");
    q.bindValue(":d", QString::fromStdString(today));
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

int ScreenTimeTracker::totalWeekSeconds() const {
    QString weekAgo = QDate::currentDate().addDays(-7).toString("yyyy-MM-dd");
    QSqlQuery q(db);
    q.prepare("SELECT SUM(seconds) FROM screen_time WHERE date >= :d");
    q.bindValue(":d", weekAgo);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

int ScreenTimeTracker::dailyAverageSeconds() const {
    QString weekAgo = QDate::currentDate().addDays(-7).toString("yyyy-MM-dd");
    QSqlQuery q(db);
    q.prepare("SELECT SUM(seconds) FROM screen_time WHERE date >= :d");
    q.bindValue(":d", weekAgo);
    if (q.exec() && q.next()) {
        int total = q.value(0).toInt();
        QSqlQuery dq(db);
        dq.prepare("SELECT COUNT(DISTINCT date) FROM screen_time WHERE date >= :d");
        dq.bindValue(":d", weekAgo);
        if (dq.exec() && dq.next()) {
            int days = dq.value(0).toInt();
            if (days > 0) return total / days;
        }
    }
    return 0;
}

std::vector<ScreenTimeTracker::DailyTotal> ScreenTimeTracker::last7Days() const {
    std::vector<DailyTotal> result;

    for (int i = 6; i >= 0; --i) {
        QDate d = QDate::currentDate().addDays(-i);
        DailyTotal dt;
        dt.date = d.toString("yyyy-MM-dd").toStdString();

        if (i == 0) {
            dt.dayLabel = "Today";
        } else {
            dt.dayLabel = d.toString("ddd").toStdString();
        }

        QSqlQuery q(db);
        q.prepare("SELECT SUM(seconds) FROM screen_time WHERE date = :d");
        q.bindValue(":d", QString::fromStdString(dt.date));
        dt.seconds = 0;
        if (q.exec() && q.next()) dt.seconds = q.value(0).toInt();

        result.push_back(dt);
    }
    return result;
}