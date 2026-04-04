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

ScreenTimeTracker::ScreenTimeTracker() {
    initDb();
    lastTickTime = std::chrono::steady_clock::now();
}

ScreenTimeTracker::~ScreenTimeTracker() {
    if (db.isOpen()) db.close();
}

void ScreenTimeTracker::initDb() {
    // Use same path as daemon: ~/.local/share/SystemMonitor/sysmon.db
    QString home = QDir::homePath();
    QString dataDir = home + "/.local/share/SystemMonitor";
    QDir().mkpath(dataDir);
    QString dbPath = dataDir + "/sysmon.db";

    db = QSqlDatabase::addDatabase("QSQLITE", "screen_time_tracker");
    db.setDatabaseName(dbPath);
    db.open();

    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS screen_time ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "app_name TEXT NOT NULL,"
           "date TEXT NOT NULL,"
           "seconds INTEGER NOT NULL DEFAULT 0,"
           "UNIQUE(app_name, date))");
    q.exec("CREATE INDEX IF NOT EXISTS idx_st_date ON screen_time(date)");

    q.exec("CREATE TABLE IF NOT EXISTS browser_tab_time ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "browser TEXT NOT NULL,"
           "site TEXT NOT NULL,"
           "date TEXT NOT NULL,"
           "seconds INTEGER NOT NULL DEFAULT 0,"
           "UNIQUE(browser, site, date))");
    q.exec("CREATE INDEX IF NOT EXISTS idx_btt_date ON browser_tab_time(date)");

    q.exec("PRAGMA journal_mode=WAL");
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
// isScreenBlanked()
//
// Returns true only when the X screen saver has actually blanked or locked
// the display. A user sitting still watching a video returns false — the
// screen is still ON, just idle from an input perspective.
// ---------------------------------------------------------------------------
bool ScreenTimeTracker::isScreenBlanked(Display *dpy) const {
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

bool ScreenTimeTracker::getFocusedAppAndTitle(std::string &appName, std::string &windowTitle) {
    appName.clear();
    windowTitle.clear();

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return false;

    if (isScreenBlanked(dpy)) {
        XCloseDisplay(dpy);
        return false;
    }

    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);

    if (focused == 0 || focused == 1) {
        XCloseDisplay(dpy);
        return false;
    }

    Atom wmClass   = XInternAtom(dpy, "WM_CLASS", 1);
    Atom netWmName = XInternAtom(dpy, "_NET_WM_NAME", 1);
    Atom utf8Str   = XInternAtom(dpy, "UTF8_STRING", 1);

    if (wmClass == 0) {
        XCloseDisplay(dpy);
        return false;
    }

    Window current = focused;
    Window root, parent;
    Window *children;
    unsigned int nChildren;

    for (int i = 0; i < 20; ++i) {
        Atom actualType;
        int actualFormat;
        unsigned long nItems, bytesAfter;
        unsigned char *prop = nullptr;

        if (XGetWindowProperty(dpy, current, wmClass, 0, 1024, 0,
                               XA_STRING, &actualType, &actualFormat,
                               &nItems, &bytesAfter, &prop) == Success && prop) {
            std::string instance(reinterpret_cast<char*>(prop));
            std::string className;
            if (nItems > instance.length() + 1)
                className = std::string(reinterpret_cast<char*>(prop) + instance.length() + 1);
            XFree(prop);

            appName = normalizeAppName(className.empty() ? instance : className);

            // Read _NET_WM_NAME (UTF-8 window title) from the same top-level window
            if (netWmName && utf8Str) {
                unsigned char *titleProp = nullptr;
                unsigned long titleItems;
                Atom at; int af; unsigned long ba;
                if (XGetWindowProperty(dpy, current, netWmName, 0, 4096, 0,
                                       utf8Str, &at, &af, &titleItems, &ba,
                                       &titleProp) == Success && titleProp) {
                    windowTitle = reinterpret_cast<char*>(titleProp);
                    XFree(titleProp);
                }
            }

            XCloseDisplay(dpy);
            return true;
        }

        if (!XQueryTree(dpy, current, &root, &parent, &children, &nChildren)) break;
        if (children) XFree(children);
        if (parent == root || parent == current) break;
        current = parent;
    }

    XCloseDisplay(dpy);
    return false;
}

bool ScreenTimeTracker::isBrowser(const std::string &appName) const {
    return appName == "google-chrome" || appName == "chromium" ||
           appName == "brave"         || appName == "firefox"  ||
           appName == "microsoft-edge";
}

std::string ScreenTimeTracker::extractSiteFromTitle(const std::string &title,
                                                     const std::string &browser) const {
    std::string t = title;

    // Strip browser suffix so we are left with the page title
    auto stripSuffix = [&](const std::string &suffix) {
        size_t pos = t.rfind(suffix);
        if (pos != std::string::npos) t = t.substr(0, pos);
    };

    if      (browser == "google-chrome")   { stripSuffix(" - Google Chrome");   stripSuffix(" \xe2\x80\x94 Google Chrome"); }
    else if (browser == "chromium")        { stripSuffix(" - Chromium"); }
    else if (browser == "brave")           { stripSuffix(" - Brave"); }
    else if (browser == "firefox")         { stripSuffix(" \xe2\x80\x94 Mozilla Firefox"); stripSuffix(" - Mozilla Firefox"); }
    else if (browser == "microsoft-edge")  { stripSuffix(" - Microsoft Edge"); }

    // Trim
    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t = t.substr(1);

    if (t.empty() || t == "New Tab" || t == "about:blank" || t == "New tab") return "";

    // Lowercase copy for matching aliases that can't be inferred from segments
    // (combined domains, non-obvious capitalization, multi-word brand names)
    std::string tl = t;
    std::transform(tl.begin(), tl.end(), tl.begin(), ::tolower);

    // Only keep entries that the segment heuristic below CANNOT handle correctly:
    //   • Sites with aliases/combined domains (twitter → X, openai → ChatGPT)
    //   • Google sub-products that share the "google" segment
    if (tl.find("google docs")     != std::string::npos) return "Google Docs";
    if (tl.find("google drive")    != std::string::npos) return "Google Drive";
    if (tl.find("google calendar") != std::string::npos) return "Google Calendar";
    if (tl.find("google meet")     != std::string::npos) return "Google Meet";
    if (tl.find("stack overflow")  != std::string::npos) return "Stack Overflow";
    if (tl.find("twitter")         != std::string::npos ||
        tl.find("x.com")           != std::string::npos) return "X (Twitter)";
    if (tl.find("chatgpt")         != std::string::npos ||
        tl.find("openai")          != std::string::npos) return "ChatGPT";
    if (tl.find("claude")          != std::string::npos ||
        tl.find("anthropic")       != std::string::npos) return "Claude";

    // ---------------------------------------------------------------
    // For unknown sites: website names are short; page content is long.
    // Split by | and - and pick the shortest segment — it is almost
    // always the brand/site name.  Prefer the last segment after |
    // when present (the most common "Page Title | Site Name" pattern).
    // ---------------------------------------------------------------

    auto trimSeg = [](const std::string &s) -> std::string {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    };

    // Collect all segments split by | and -
    std::vector<std::string> segments;
    std::string cur;
    for (char c : t) {
        if (c == '|' || c == '-') {
            std::string seg = trimSeg(cur);
            if (!seg.empty()) segments.push_back(seg);
            cur.clear();
        } else {
            cur += c;
        }
    }
    {
        std::string seg = trimSeg(cur);
        if (!seg.empty()) segments.push_back(seg);
    }

    if (segments.size() <= 1) {
        // No separators — return title as-is (truncated if long)
        if (t.size() > 60) t = t.substr(0, 57) + "...";
        return t;
    }

    // If there was a | separator, the last | segment is very reliable
    {
        size_t pos = t.rfind('|');
        if (pos != std::string::npos) {
            std::string seg = trimSeg(t.substr(pos + 1));
            if (!seg.empty() && seg.size() <= 40)
                return seg;
        }
    }

    // Otherwise find the shortest segment (site names are concise)
    std::string shortest;
    for (const auto &seg : segments) {
        if (shortest.empty() || seg.size() < shortest.size())
            shortest = seg;
    }
    if (!shortest.empty() && shortest.size() <= 35)
        return shortest;

    // Last resort: first segment (truncated)
    std::string first = segments.front();
    if (first.size() > 60) first = first.substr(0, 57) + "...";
    return first;
}

void ScreenTimeTracker::tick() {
    auto now = std::chrono::steady_clock::now();
    int elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTickTime).count();
    lastTickTime = now;

    // Clamp elapsed to reasonable range (handles app being paused/resumed)
    if (elapsed <= 0) elapsed = 1;
    if (elapsed > 10) elapsed = 2; // If more than 10s gap, something stalled — record minimal

    std::string app, windowTitle;
    if (!getFocusedAppAndTitle(app, windowTitle)) {
        currentFocusApp.clear();
        return;
    }

    currentFocusApp = app;

    // Only record if daemon is NOT running (avoid double counting)
    bool daemonRunning = false;
    FILE *fp = popen("pgrep -x sysmon-tracker 2>/dev/null", "r");
    if (fp) {
        char buf[32];
        if (fgets(buf, sizeof(buf), fp)) daemonRunning = true;
        pclose(fp);
    }

    if (daemonRunning) return; // Let daemon handle recording

    recordTick(app, elapsed);

    // Also track which browser tab is active
    if (isBrowser(app) && !windowTitle.empty()) {
        std::string site = extractSiteFromTitle(windowTitle, app);
        if (!site.empty()) {
            recordBrowserTab(app, site, elapsed);
        }
    }
}

void ScreenTimeTracker::recordTick(const std::string &appName, int seconds) {
    std::string today = QDate::currentDate().toString("yyyy-MM-dd").toStdString();

    QSqlQuery q(db);
    q.prepare("INSERT INTO screen_time (app_name, date, seconds) "
              "VALUES (:app, :date, :sec) "
              "ON CONFLICT(app_name, date) DO UPDATE SET seconds = seconds + :sec2");
    q.bindValue(":app", QString::fromStdString(appName));
    q.bindValue(":date", QString::fromStdString(today));
    q.bindValue(":sec", seconds);
    q.bindValue(":sec2", seconds);
    q.exec();
}

void ScreenTimeTracker::recordBrowserTab(const std::string &browser, const std::string &site, int seconds) {
    if (browser.empty() || site.empty() || seconds <= 0) return;
    std::string today = QDate::currentDate().toString("yyyy-MM-dd").toStdString();

    QSqlQuery q(db);
    q.prepare("INSERT INTO browser_tab_time (browser, site, date, seconds) "
              "VALUES (:b, :s, :d, :sec) "
              "ON CONFLICT(browser, site, date) DO UPDATE SET seconds = seconds + :sec2");
    q.bindValue(":b",    QString::fromStdString(browser));
    q.bindValue(":s",    QString::fromStdString(site));
    q.bindValue(":d",    QString::fromStdString(today));
    q.bindValue(":sec",  seconds);
    q.bindValue(":sec2", seconds);
    q.exec();
}

std::map<std::string, std::vector<ScreenTimeTracker::BrowserTabTime>>
ScreenTimeTracker::browserTabTodayStats() const {
    std::map<std::string, std::vector<BrowserTabTime>> result;
    std::string today = QDate::currentDate().toString("yyyy-MM-dd").toStdString();

    QSqlQuery q(db);
    q.prepare("SELECT browser, site, seconds FROM browser_tab_time "
              "WHERE date = :d ORDER BY browser, seconds DESC");
    q.bindValue(":d", QString::fromStdString(today));
    if (q.exec()) {
        while (q.next()) {
            BrowserTabTime t;
            t.browser      = q.value(0).toString().toStdString();
            t.site         = q.value(1).toString().toStdString();
            t.todaySeconds = q.value(2).toInt();
            t.weekSeconds  = 0;
            result[t.browser].push_back(t);
        }
    }
    return result;
}

std::map<std::string, std::vector<ScreenTimeTracker::BrowserTabTime>>
ScreenTimeTracker::browserTabWeeklyStats() const {
    std::map<std::string, std::vector<BrowserTabTime>> result;
    QString weekAgo = QDate::currentDate().addDays(-7).toString("yyyy-MM-dd");

    QSqlQuery q(db);
    q.prepare("SELECT browser, site, SUM(seconds) as total FROM browser_tab_time "
              "WHERE date >= :d GROUP BY browser, site ORDER BY browser, total DESC");
    q.bindValue(":d", weekAgo);
    if (q.exec()) {
        while (q.next()) {
            BrowserTabTime t;
            t.browser      = q.value(0).toString().toStdString();
            t.site         = q.value(1).toString().toStdString();
            t.todaySeconds = 0;
            t.weekSeconds  = q.value(2).toInt();
            result[t.browser].push_back(t);
        }
    }
    return result;
}

std::vector<ScreenTimeTracker::AppScreenTime> ScreenTimeTracker::todayStats() const {
    std::vector<AppScreenTime> result;
    std::string today = QDate::currentDate().toString("yyyy-MM-dd").toStdString();

    QSqlQuery q(db);
    q.prepare("SELECT app_name, seconds FROM screen_time WHERE date = :d ORDER BY seconds DESC");
    q.bindValue(":d", QString::fromStdString(today));
    if (q.exec()) {
        while (q.next()) {
            AppScreenTime s;
            s.appName = q.value(0).toString().toStdString();
            s.todaySeconds = q.value(1).toInt();

            QString weekAgo = QDate::currentDate().addDays(-7).toString("yyyy-MM-dd");
            QSqlQuery wq(db);
            wq.prepare("SELECT SUM(seconds) FROM screen_time WHERE app_name = :app AND date >= :d");
            wq.bindValue(":app", QString::fromStdString(s.appName));
            wq.bindValue(":d", weekAgo);
            s.weekSeconds = 0;
            if (wq.exec() && wq.next()) s.weekSeconds = wq.value(0).toInt();

            result.push_back(s);
        }
    }
    return result;
}

std::vector<ScreenTimeTracker::AppScreenTime> ScreenTimeTracker::weeklyStats() const {
    std::vector<AppScreenTime> result;
    QString weekAgo = QDate::currentDate().addDays(-7).toString("yyyy-MM-dd");

    QSqlQuery q(db);
    q.prepare("SELECT app_name, SUM(seconds) as total FROM screen_time "
              "WHERE date >= :d GROUP BY app_name ORDER BY total DESC");
    q.bindValue(":d", weekAgo);
    if (q.exec()) {
        while (q.next()) {
            AppScreenTime s;
            s.appName = q.value(0).toString().toStdString();
            s.weekSeconds = q.value(1).toInt();
            s.todaySeconds = 0;
            result.push_back(s);
        }
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