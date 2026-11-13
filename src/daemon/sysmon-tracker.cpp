// sysmon-tracker: background screen-time recorder.
//
// Once a second it asks FocusDetector which app is in front, and accumulates
// the elapsed time per (app, site, date, hour). The time is written to the
// shared SQLite database whenever the key changes and at least every 5 s.

#include "core/FocusDetector.h"
#include "core/SysmonDb.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;
static void sigHandler(int) { g_running = 0; }

namespace {

struct Accumulator {
    SysmonDb *db = nullptr;
    std::string app, tab, date;
    int hour = -1;
    long long accumMs = 0;
    long long lastFlushMs = 0;

    bool sameKey(const std::string &a, const std::string &t, const std::string &d, int h) const {
        return app == a && tab == t && date == d && hour == h;
    }

    // Write whole seconds; keep the sub-second remainder so nothing is lost.
    void flush() {
        if (!app.empty() && accumMs >= 1000) {
            int secs = static_cast<int>(accumMs / 1000);
            db->recordScreenTime(app, tab, date, hour, secs);
            accumMs -= static_cast<long long>(secs) * 1000;
        }
        lastFlushMs = TimeUtil::nowMs();
    }

    void clear() {
        flush();
        app.clear(); tab.clear(); date.clear(); hour = -1;
        accumMs = 0;
    }
};

Accumulator *g_accum = nullptr;

void writePidFile() {
    std::ofstream f(SysmonDb::pidFilePath());
    if (f) f << getpid() << "\n";
}

} // namespace

int main() {
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGHUP, SIG_IGN);
    setvbuf(stdout, nullptr, _IOLBF, 0);

    SysmonDb db;
    if (!db.isOpen()) {
        fprintf(stderr, "Cannot open database at %s: %s\n", db.path().c_str(), db.lastError().c_str());
        return 1;
    }

    FocusDetector detector;
    Accumulator acc;
    acc.db = &db;
    acc.lastFlushMs = TimeUtil::nowMs();
    g_accum = &acc;
    FocusDetector::onDisplayLost = [] { if (g_accum) g_accum->flush(); };

    writePidFile();
    printf("sysmon-tracker started (PID %d), database %s, X display %s\n",
           getpid(), db.path().c_str(), detector.hasDisplay() ? "connected" : "not available yet");

    FocusDetector::Source lastSource = FocusDetector::Source::None;
    bool lastScreenOff = false;
    long long lastTickMs = TimeUtil::nowMs();

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        long long now = TimeUtil::nowMs();
        long long elapsed = now - lastTickMs;
        lastTickMs = now;
        // Suspend/resume or a stalled machine: do not credit a huge gap to one app.
        if (elapsed > 5000) elapsed = 1000;

        FocusDetector::Result r = detector.poll();

        if (r.screenOff != lastScreenOff) {
            printf("Screen %s\n", r.screenOff ? "off/locked, pausing" : "on, resuming");
            lastScreenOff = r.screenOff;
        }
        if (r.source != lastSource && r.source != FocusDetector::Source::None) {
            printf("Tracking via %s\n", FocusDetector::sourceName(r.source));
            lastSource = r.source;
        }

        if (r.screenOff || r.app.empty()) {
            if (!acc.app.empty()) acc.clear();
            continue;
        }

        std::string today = TimeUtil::dateString(0);
        int hour = TimeUtil::currentHour();

        if (!acc.sameKey(r.app, r.tab, today, hour)) {
            acc.flush();
            long long carry = acc.app.empty() ? 0 : acc.accumMs;  // sub-second remainder
            acc.app = r.app; acc.tab = r.tab; acc.date = today; acc.hour = hour;
            acc.accumMs = carry;
        }
        acc.accumMs += elapsed;

        if (now - acc.lastFlushMs >= 5000) acc.flush();
    }

    acc.flush();
    unlink(SysmonDb::pidFilePath().c_str());
    printf("sysmon-tracker stopped.\n");
    return 0;
}
