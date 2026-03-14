#include "ScreenTimeTracker.h"
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDate>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

ScreenTimeTracker::ScreenTimeTracker() {
    initDb();
}

ScreenTimeTracker::~ScreenTimeTracker() {
    if (db.isOpen()) db.close();
}

void ScreenTimeTracker::initDb() {
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
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
}

std::string ScreenTimeTracker::normalizeAppName(const std::string &name) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);

    if (n.find("brave") != std::string::npos) return "brave";
    if (n.find("firefox") != std::string::npos) return "firefox";
    if (n.find("chrome") != std::string::npos) return "chrome";
    if (n.find("chromium") != std::string::npos) return "chromium";
    if (n == "code" || n.find("code") == 0) return "vscode";
    if (n.find("slack") != std::string::npos) return "slack";
    if (n.find("discord") != std::string::npos) return "discord";
    if (n.find("spotify") != std::string::npos) return "spotify";
    if (n.find("nemo") != std::string::npos) return "nemo (files)";
    if (n.find("terminal") != std::string::npos || n.find("gnome-terminal") != std::string::npos
        || n.find("xterm") != std::string::npos || n.find("konsole") != std::string::npos)
        return "terminal";
    if (n.find("libreoffice") != std::string::npos) return "libreoffice";
    if (n.find("gimp") != std::string::npos) return "gimp";
    if (n.find("vlc") != std::string::npos) return "vlc";
    if (n.find("telegram") != std::string::npos) return "telegram";
    if (n.find("thunderbird") != std::string::npos) return "thunderbird";

    return n;
}

std::string ScreenTimeTracker::getFocusedAppName() {
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) return "";

    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);

    if (focused == None || focused == PointerRoot) {
        XCloseDisplay(dpy);
        return "";
    }

    // Walk up to find the top-level window
    Atom actualType;
    int actualFormat;
    unsigned long nItems, bytesAfter;
    unsigned char *prop = nullptr;

    // Try _NET_WM_PID on the focused window and its parents
    Atom pidAtom = XInternAtom(dpy, "_NET_WM_PID", True);
    Atom wmClass = XInternAtom(dpy, "WM_CLASS", True);

    // Walk up to the top-level window
    Window current = focused;
    Window root, parent;
    Window *children;
    unsigned int nChildren;

    for (int i = 0; i < 20; ++i) { // Max depth to avoid infinite loops
        if (XGetWindowProperty(dpy, current, wmClass, 0, 1024, False,
                               XA_STRING, &actualType, &actualFormat,
                               &nItems, &bytesAfter, &prop) == Success && prop) {
            // WM_CLASS contains two null-terminated strings: instance and class
            // The class name (second string) is what we want
            std::string instance(reinterpret_cast<char*>(prop));
            std::string className;
            if (nItems > instance.length() + 1) {
                className = std::string(reinterpret_cast<char*>(prop) + instance.length() + 1);
            }
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
    std::string app = getFocusedAppName();
    if (app.empty()) return;

    currentFocusApp = app;

    // Record ~1.5 seconds (the refresh interval) for this app
    recordTick(app, 2); // Round to 2 seconds for simplicity
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

            // Also get weekly
            QSqlQuery wq(db);
            QString weekAgo = QDate::currentDate().addDays(-7).toString("yyyy-MM-dd");
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
        // Count how many distinct days have data
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

        // Use actual short day name from Qt
        if (i == 0) {
            dt.dayLabel = "Today";
        } else {
            dt.dayLabel = d.toString("ddd").toStdString(); // "Mon", "Tue", etc.
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