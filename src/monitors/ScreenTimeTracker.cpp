#include "ScreenTimeTracker.h"

#include <fstream>
#include <signal.h>

ScreenTimeTracker::ScreenTimeTracker(SysmonDb &db, FocusDetector &detector)
    : db_(db), detector_(detector) {
    lastFlushMs_ = TimeUtil::nowMs();
}

bool ScreenTimeTracker::checkDaemon() {
    // The daemon writes its PID next to the database. Verify the PID is alive
    // and really is sysmon-tracker (PIDs get reused).
    std::ifstream f(SysmonDb::pidFilePath());
    int pid = 0;
    if (!(f >> pid) || pid <= 0) return false;
    if (kill(pid, 0) != 0) return false;
    std::ifstream c("/proc/" + std::to_string(pid) + "/comm");
    std::string comm;
    return std::getline(c, comm) && comm == "sysmon-tracker";
}

void ScreenTimeTracker::flush() {
    if (!accApp_.empty() && accMs_ >= 1000) {
        int secs = static_cast<int>(accMs_ / 1000);
        db_.recordScreenTime(accApp_, accTab_, accDate_, accHour_, secs);
        accMs_ -= static_cast<long long>(secs) * 1000;
    }
    lastFlushMs_ = TimeUtil::nowMs();
}

void ScreenTimeTracker::tick(long long elapsedMs) {
    long long now = TimeUtil::nowMs();
    if (lastDaemonCheckMs_ == 0 || now - lastDaemonCheckMs_ > 5000) {
        daemonRunning_ = checkDaemon();
        lastDaemonCheckMs_ = now;
    }

    current_ = detector_.poll();

    if (daemonRunning_) {
        // The daemon owns the database writes; drop anything we were accumulating.
        if (!accApp_.empty()) { flush(); accApp_.clear(); accMs_ = 0; }
        return;
    }

    if (elapsedMs > 5000) elapsedMs = 1500;

    if (current_.screenOff || current_.app.empty()) {
        if (!accApp_.empty()) { flush(); accApp_.clear(); accTab_.clear(); accMs_ = 0; }
        return;
    }

    std::string today = TimeUtil::dateString(0);
    int hour = TimeUtil::currentHour();
    if (accApp_ != current_.app || accTab_ != current_.tab || accDate_ != today || accHour_ != hour) {
        flush();
        long long carry = accApp_.empty() ? 0 : accMs_;
        accApp_ = current_.app; accTab_ = current_.tab; accDate_ = today; accHour_ = hour;
        accMs_ = carry;
    }
    accMs_ += elapsedMs;
    if (now - lastFlushMs_ >= 5000) flush();
}

ScreenTimeStats ScreenTimeTracker::stats() const {
    ScreenTimeStats s;
    std::string today = TimeUtil::dateString(0);
    std::string weekStart = TimeUtil::dateString(6);  // 7 days including today

    s.today = db_.appTotals(today, today);
    s.week = db_.appTotals(weekStart, today);
    s.last7 = db_.dailyTotals(7);
    s.hourly = db_.hourlyTotals(today);
    s.todaySeconds = db_.totalSeconds(today, today);
    s.weekSeconds = db_.totalSeconds(weekStart, today);
    int days = db_.daysWithData(weekStart, today);
    s.dailyAverageSeconds = days > 0 ? s.weekSeconds / days : 0;

    s.currentApp = current_.app;
    s.currentTab = current_.tab;
    s.screenOff = current_.screenOff;
    s.daemonRunning = daemonRunning_;
    s.source = FocusDetector::sourceName(current_.source);
    return s;
}
