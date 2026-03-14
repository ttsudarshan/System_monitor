#include "BatteryTracker.h"
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDate>
#include <QDateTime>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unistd.h>
#include <algorithm>

namespace fs = std::filesystem;

BatteryTracker::BatteryTracker() {
    initDb();
    // Check if battery exists
    batteryPresent = fs::exists("/sys/class/power_supply/BAT0");
}

BatteryTracker::~BatteryTracker() {
    if (db.isOpen()) db.close();
}

void BatteryTracker::initDb() {
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString dbPath = dataDir + "/sysmon.db";

    db = QSqlDatabase::addDatabase("QSQLITE", "battery_tracker");
    db.setDatabaseName(dbPath);
    db.open();

    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS battery_drain ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
           "app_name TEXT NOT NULL,"
           "date TEXT NOT NULL,"
           "watt_hours REAL NOT NULL DEFAULT 0,"
           "UNIQUE(app_name, date))");

    q.exec("CREATE INDEX IF NOT EXISTS idx_drain_date ON battery_drain(date)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_drain_app ON battery_drain(app_name)");
}

double BatteryTracker::readSystemPowerW() {
    // Try power_now (microwatts)
    std::ifstream f("/sys/class/power_supply/BAT0/power_now");
    if (f.is_open()) {
        uint64_t uW;
        f >> uW;
        return uW / 1000000.0;
    }
    // Fallback: voltage * current
    std::ifstream vf("/sys/class/power_supply/BAT0/voltage_now");
    std::ifstream cf("/sys/class/power_supply/BAT0/current_now");
    if (vf.is_open() && cf.is_open()) {
        uint64_t uV, uA;
        vf >> uV; cf >> uA;
        return (uV / 1e6) * (uA / 1e6);
    }
    return 0.0;
}

uint64_t BatteryTracker::readTotalCpuTime() {
    std::ifstream f("/proc/stat");
    if (!f.is_open()) return 0;
    std::string line;
    std::getline(f, line);
    std::istringstream ss(line);
    std::string label;
    ss >> label;
    uint64_t total = 0, val;
    while (ss >> val) total += val;
    return total;
}

std::string BatteryTracker::normalizeAppName(const std::string &comm) {
    // Strip common suffixes and normalize
    std::string name = comm;

    // Remove "Web Content", "Socket Process" etc. type suffixes for browsers
    // Just use base executable name
    // Convert to lowercase for grouping
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);

    // Group known multi-process apps
    if (name.find("brave") != std::string::npos) return "brave";
    if (name.find("firefox") != std::string::npos) return "firefox";
    if (name.find("chrome") != std::string::npos) return "chrome";
    if (name.find("chromium") != std::string::npos) return "chromium";
    if (name == "code" || name.find("code") == 0) return "vscode";
    if (name.find("slack") != std::string::npos) return "slack";
    if (name.find("discord") != std::string::npos) return "discord";
    if (name.find("spotify") != std::string::npos) return "spotify";
    if (name.find("steam") != std::string::npos) return "steam";
    if (name.find("telegram") != std::string::npos) return "telegram";
    if (name.find("vlc") != std::string::npos) return "vlc";
    if (name.find("gimp") != std::string::npos) return "gimp";
    if (name.find("libreoffice") != std::string::npos) return "libreoffice";
    if (name.find("thunderbird") != std::string::npos) return "thunderbird";

    return name;
}

void BatteryTracker::tick() {
    if (!batteryPresent) return;

    double systemWatts = readSystemPowerW();
    uint64_t totalCpuNow = readTotalCpuTime();
    int numCpus = sysconf(_SC_NPROCESSORS_ONLN);

    if (prevTotalCpu == 0) {
        // First tick — just collect baseline
        prevTotalCpu = totalCpuNow;

        // Scan all PIDs for baseline
        for (auto &entry : fs::directory_iterator("/proc")) {
            std::string fname = entry.path().filename().string();
            int pid;
            try { pid = std::stoi(fname); } catch (...) { continue; }

            std::ifstream sf(entry.path().string() + "/stat");
            if (!sf.is_open()) continue;
            std::string line;
            std::getline(sf, line);
            auto lp = line.find('(');
            auto rp = line.rfind(')');
            if (lp == std::string::npos || rp == std::string::npos) continue;

            std::string comm = line.substr(lp + 1, rp - lp - 1);
            std::istringstream rest(line.substr(rp + 2));
            std::string state;
            uint64_t dummy, utime, stime;
            rest >> state;
            for (int i = 0; i < 10; ++i) rest >> dummy;
            rest >> utime >> stime;

            PidCpuData d;
            d.appName = normalizeAppName(comm);
            d.utime = utime;
            d.stime = stime;
            prevPidData[pid] = d;
        }
        return;
    }

    uint64_t sysDelta = totalCpuNow - prevTotalCpu;
    if (sysDelta == 0) return;

    // Scan all PIDs and compute CPU delta per app
    std::unordered_map<std::string, double> appCpuShare; // app -> fraction of total CPU
    std::unordered_map<int, PidCpuData> newPidData;

    for (auto &entry : fs::directory_iterator("/proc")) {
        std::string fname = entry.path().filename().string();
        int pid;
        try { pid = std::stoi(fname); } catch (...) { continue; }

        std::ifstream sf(entry.path().string() + "/stat");
        if (!sf.is_open()) continue;
        std::string line;
        std::getline(sf, line);
        auto lp = line.find('(');
        auto rp = line.rfind(')');
        if (lp == std::string::npos || rp == std::string::npos) continue;

        std::string comm = line.substr(lp + 1, rp - lp - 1);
        std::istringstream rest(line.substr(rp + 2));
        std::string state;
        uint64_t dummy, utime, stime;
        rest >> state;
        for (int i = 0; i < 10; ++i) rest >> dummy;
        rest >> utime >> stime;

        std::string appName = normalizeAppName(comm);

        PidCpuData cur;
        cur.appName = appName;
        cur.utime = utime;
        cur.stime = stime;
        newPidData[pid] = cur;

        auto it = prevPidData.find(pid);
        if (it != prevPidData.end()) {
            uint64_t procDelta = (utime + stime) - (it->second.utime + it->second.stime);
            double share = static_cast<double>(procDelta) / sysDelta;
            appCpuShare[appName] += share;
        }
    }

    prevPidData = std::move(newPidData);
    prevTotalCpu = totalCpuNow;

    // Compute drain % per app and Wh consumed this tick
    // Tick interval is ~1.5s
    double tickHours = 1.5 / 3600.0;
    liveDrain.clear();

    double totalShare = 0;
    for (auto &[app, share] : appCpuShare)
        totalShare += share;

    if (totalShare <= 0) return;

    std::string today = QDate::currentDate().toString("yyyy-MM-dd").toStdString();

    QSqlQuery q(db);
    for (auto &[app, share] : appCpuShare) {
        double pct = (share / totalShare) * 100.0;
        liveDrain[app] = pct;

        double appWatts = (share / totalShare) * systemWatts;
        double whThisTick = appWatts * tickHours;

        if (whThisTick > 0.0001) {
            q.prepare("INSERT INTO battery_drain (app_name, date, watt_hours) "
                      "VALUES (:app, :date, :wh) "
                      "ON CONFLICT(app_name, date) DO UPDATE SET watt_hours = watt_hours + :wh2");
            q.bindValue(":app", QString::fromStdString(app));
            q.bindValue(":date", QString::fromStdString(today));
            q.bindValue(":wh", whThisTick);
            q.bindValue(":wh2", whThisTick);
            q.exec();
        }
    }
}

std::vector<BatteryTracker::AppDrain> BatteryTracker::currentDrain() const {
    std::vector<AppDrain> result;
    std::string today = QDate::currentDate().toString("yyyy-MM-dd").toStdString();

    for (auto &[app, pct] : liveDrain) {
        AppDrain d;
        d.appName = app;
        d.drainPercent = pct;
        d.totalWhToday = 0;
        d.totalWhWeek = 0;

        // Query today's total
        QSqlQuery q(db);
        q.prepare("SELECT watt_hours FROM battery_drain WHERE app_name = :app AND date = :date");
        q.bindValue(":app", QString::fromStdString(app));
        q.bindValue(":date", QString::fromStdString(today));
        if (q.exec() && q.next()) d.totalWhToday = q.value(0).toDouble();

        // Query week total
        QString weekAgo = QDate::currentDate().addDays(-7).toString("yyyy-MM-dd");
        q.prepare("SELECT SUM(watt_hours) FROM battery_drain WHERE app_name = :app AND date >= :d");
        q.bindValue(":app", QString::fromStdString(app));
        q.bindValue(":d", weekAgo);
        if (q.exec() && q.next()) d.totalWhWeek = q.value(0).toDouble();

        result.push_back(d);
    }

    std::sort(result.begin(), result.end(), [](const AppDrain &a, const AppDrain &b) {
        return a.drainPercent > b.drainPercent;
    });
    return result;
}

std::vector<BatteryTracker::DailyAppDrain> BatteryTracker::dailyStats(const std::string &date) const {
    std::vector<DailyAppDrain> result;
    QSqlQuery q(db);
    q.prepare("SELECT app_name, watt_hours FROM battery_drain WHERE date = :d ORDER BY watt_hours DESC");
    q.bindValue(":d", QString::fromStdString(date));
    if (q.exec()) {
        while (q.next()) {
            DailyAppDrain d;
            d.appName = q.value(0).toString().toStdString();
            d.date = date;
            d.totalWh = q.value(1).toDouble();
            result.push_back(d);
        }
    }
    return result;
}

std::vector<BatteryTracker::DailyAppDrain> BatteryTracker::weeklyStats() const {
    std::vector<DailyAppDrain> result;
    QString weekAgo = QDate::currentDate().addDays(-7).toString("yyyy-MM-dd");
    QSqlQuery q(db);
    q.prepare("SELECT app_name, SUM(watt_hours) as total FROM battery_drain "
              "WHERE date >= :d GROUP BY app_name ORDER BY total DESC");
    q.bindValue(":d", weekAgo);
    if (q.exec()) {
        while (q.next()) {
            DailyAppDrain d;
            d.appName = q.value(0).toString().toStdString();
            d.date = "week";
            d.totalWh = q.value(1).toDouble();
            result.push_back(d);
        }
    }
    return result;
}