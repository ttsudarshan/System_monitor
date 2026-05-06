#include "sysmon-tracker-core.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>

static volatile bool running = true;
static const int TICK_MS = 1000;

void sigHandler(int) { running = false; }

int main() {
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::cerr << "Warning: Cannot open X display. Defaulting to Wayland tracking." << std::endl;
    }

    sqlite3 *db = openDb();
    if (!db) {
        if (dpy) XCloseDisplay(dpy);
        return 1;
    }

    std::cout << "sysmon-tracker started (PID " << getpid() << ")" << std::endl;

    auto lastTick = std::chrono::steady_clock::now();
    std::string lastApp;
    std::string lastTab;
    int accumSeconds = 0;
    std::string lastDate = getCurrentDate();

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));

        auto now = std::chrono::steady_clock::now();
        int elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTick).count();
        lastTick = now;

        std::string today = getCurrentDate();
        if (today != lastDate) {
            if (!lastApp.empty() && accumSeconds > 0) {
                recordTime(db, lastApp, lastTab, lastDate, accumSeconds);
                accumSeconds = 0;
            }
            lastDate = today;
        }

        if (isScreenBlanked(dpy)) {
            if (!lastApp.empty() && accumSeconds > 0) {
                recordTime(db, lastApp, lastTab, today, accumSeconds);
                accumSeconds = 0;
            }
            lastApp.clear();
            lastTab.clear();
            continue;
        }

        std::string app = getFocusedApp(dpy);
        if (app.empty()) {
            if (!lastApp.empty() && accumSeconds > 0) {
                recordTime(db, lastApp, lastTab, today, accumSeconds);
                accumSeconds = 0;
            }
            lastApp.clear();
            lastTab.clear();
            continue;
        }

        // Read window title for browser tab tracking (X11/XWayland only)
        std::string title = dpy ? getWindowTitle(dpy) : "";
        std::string tab   = extractTabName(app, title);

        if (app == lastApp && tab == lastTab) {
            accumSeconds += elapsed;
            if (accumSeconds >= 2) {
                recordTime(db, app, tab, today, accumSeconds);
                accumSeconds = 0;
            }
        } else {
            if (!lastApp.empty() && accumSeconds > 0) {
                recordTime(db, lastApp, lastTab, today, accumSeconds);
            }
            lastApp = app;
            lastTab = tab;
            accumSeconds = elapsed;
        }
    }

    // Flush any remaining accumulated time on clean exit
    if (!lastApp.empty() && accumSeconds > 0) {
        recordTime(db, lastApp, lastTab, getCurrentDate(), accumSeconds);
    }

    sqlite3_close(db);
    if (dpy) XCloseDisplay(dpy);

    std::cout << "sysmon-tracker stopped." << std::endl;
    return 0;
}
