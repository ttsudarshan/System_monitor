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

static const int IDLE_THRESHOLD_MS = 90000; // 90 seconds

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

std::string ScreenTimeTracker::getFocusedAppName() {
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return "";

    // Check idle time first
    XScreenSaverInfo *info = XScreenSaverAllocInfo();
    if (info) {
        int evBase, errBase;
        if (XScreenSaverQueryExtension(dpy, &evBase, &errBase)) {
            XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), info);
            if (info->idle > IDLE_THRESHOLD_MS) {
                XFree(info);
                XCloseDisplay(dpy);
                return ""; // User is idle
            }
        }
        XFree(info);
    }

    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);

    if (focused == 0 || focused == 1) {
        XCloseDisplay(dpy);
        return "";
    }

    Atom wmClass = XInternAtom(dpy, "WM_CLASS", 1);
    if (wmClass == 0) {
        XCloseDisplay(dpy);
        return "";
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
            XCloseDisplay(dpy);
            return normalizeAppName(className.empty() ? instance : className);
        }

        if (!XQueryTree(dpy, current, &root, &parent, &children, &nChildren)) break;
        if (children) XFree(children);
        if (parent == root || parent == current) break;
        current = parent;
    }

    XCloseDisplay(dpy);
    return "";
}

void ScreenTimeTracker::tick() {
    auto now = std::chrono::steady_clock::now();
    int elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTickTime).count();
    lastTickTime = now;

    // Clamp elapsed to reasonable range (handles app being paused/resumed)
    if (elapsed <= 0) elapsed = 1;
    if (elapsed > 10) elapsed = 2; // If more than 10s gap, something stalled — record minimal

    std::string app = getFocusedAppName();
    if (app.empty()) {
        currentFocusApp.clear();
        return;
    }

    currentFocusApp = app;

    // Only record if daemon is NOT running (avoid double counting)
    // Check if daemon is active by looking for its PID
    bool daemonRunning = false;
    FILE *fp = popen("pgrep -x sysmon-tracker 2>/dev/null", "r");
    if (fp) {
        char buf[32];
        if (fgets(buf, sizeof(buf), fp)) daemonRunning = true;
        pclose(fp);
    }

    if (daemonRunning) return; // Let daemon handle recording

    // Record the exact elapsed time
    recordTick(app, elapsed);
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