// sysmon-tracker: Background daemon for accurate screen time tracking
// Runs independently of the System Monitor GUI
// Records focused app + idle detection to SQLite

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>
#include <csignal>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/scrnsaver.h>

// We use raw SQLite3 instead of Qt to keep the daemon lightweight
#include <sqlite3.h>

// ─── Globals ────────────────────────────────────────────────────

static volatile bool running = true;
static const int TICK_MS = 1000; // Check every 1 second
static const int IDLE_THRESHOLD_MS = 90000; // 90 seconds idle = stop counting

void sigHandler(int) { running = false; }

// ─── App Name Normalization ─────────────────────────────────────

std::string normalize(const std::string &name) {
    std::string n = name;
    for (auto &c : n) c = tolower(c);

    if (n.find("brave") != std::string::npos) return "brave";
    if (n.find("firefox") != std::string::npos) return "firefox";
    if (n.find("chrome") != std::string::npos && n.find("chromium") == std::string::npos) return "google-chrome";
    if (n.find("chromium") != std::string::npos) return "chromium";
    if (n == "code" || n.find("code") == 0) return "vscode";
    if (n.find("slack") != std::string::npos) return "slack";
    if (n.find("discord") != std::string::npos) return "discord";
    if (n.find("spotify") != std::string::npos) return "spotify";
    if (n.find("telegram") != std::string::npos) return "telegram";
    if (n.find("thunderbird") != std::string::npos) return "thunderbird";
    if (n.find("libreoffice") != std::string::npos) return "libreoffice";
    if (n.find("gimp") != std::string::npos) return "gimp";
    if (n.find("vlc") != std::string::npos) return "vlc";
    if (n.find("steam") != std::string::npos) return "steam";
    if (n.find("nemo") != std::string::npos) return "nemo";
    if (n.find("nautilus") != std::string::npos) return "nautilus";
    if (n.find("cinnamon") != std::string::npos) return "cinnamon";
    if (n.find("gnome-terminal") != std::string::npos) return "terminal";
    if (n.find("xterm") != std::string::npos) return "terminal";
    if (n.find("konsole") != std::string::npos) return "terminal";
    if (n.find("tilix") != std::string::npos) return "terminal";
    if (n.find("alacritty") != std::string::npos) return "terminal";
    if (n.find("kitty") != std::string::npos) return "terminal";
    if (n.find("gnome-system") != std::string::npos) return "gnome-system-monitor";
    if (n.find("systemmonitor") != std::string::npos) return "system monitor";
    if (n.find("microsoft-edge") != std::string::npos || n.find("msedge") != std::string::npos) return "microsoft-edge";
    if (n.find("obs") != std::string::npos) return "obs-studio";
    if (n.find("zoom") != std::string::npos) return "zoom";
    if (n.find("teams") != std::string::npos) return "teams";
    return name;
}

// ─── X11: Get Focused App ───────────────────────────────────────

std::string getFocusedApp(Display *dpy) {
    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);

    if (focused == 0 || focused == 1) return ""; // None or PointerRoot

    Atom wmClass = XInternAtom(dpy, "WM_CLASS", 1);
    if (wmClass == 0) return "";

    // Walk up to find WM_CLASS
    Window current = focused;
    Window root, parent;
    Window *children;
    unsigned int nChildren;

    for (int depth = 0; depth < 20; ++depth) {
        Atom actualType;
        int actualFormat;
        unsigned long nItems, bytesAfter;
        unsigned char *prop = nullptr;

        if (XGetWindowProperty(dpy, current, wmClass, 0, 1024, 0,
                               XA_STRING, &actualType, &actualFormat,
                               &nItems, &bytesAfter, &prop) == Success && prop) {
            // WM_CLASS: two null-terminated strings (instance, class)
            std::string instance(reinterpret_cast<char*>(prop));
            std::string className;
            if (nItems > instance.length() + 1)
                className = std::string(reinterpret_cast<char*>(prop) + instance.length() + 1);
            XFree(prop);
            return normalize(className.empty() ? instance : className);
        }

        if (!XQueryTree(dpy, current, &root, &parent, &children, &nChildren)) break;
        if (children) XFree(children);
        if (parent == root || parent == current) break;
        current = parent;
    }

    return "";
}

// ─── X11: Get Idle Time ─────────────────────────────────────────

long getIdleMs(Display *dpy) {
    XScreenSaverInfo *info = XScreenSaverAllocInfo();
    if (!info) return 0;

    int eventBase, errorBase;
    if (!XScreenSaverQueryExtension(dpy, &eventBase, &errorBase)) {
        XFree(info);
        return 0;
    }

    XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), info);
    long idle = info->idle; // milliseconds since last input
    XFree(info);
    return idle;
}

// ─── Database ───────────────────────────────────────────────────

std::string getDbPath() {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }
    std::string dir = std::string(home) + "/.local/share/SystemMonitor";
    mkdir(dir.c_str(), 0755);
    return dir + "/sysmon.db";
}

sqlite3* openDb() {
    sqlite3 *db = nullptr;
    std::string path = getDbPath();
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Failed to open DB: " << sqlite3_errmsg(db) << std::endl;
        return nullptr;
    }

    // Create table if needed
    const char *sql =
        "CREATE TABLE IF NOT EXISTS screen_time ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  app_name TEXT NOT NULL,"
        "  date TEXT NOT NULL,"
        "  seconds INTEGER NOT NULL DEFAULT 0,"
        "  UNIQUE(app_name, date)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_st_date ON screen_time(date);";

    char *err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) {
        std::cerr << "SQL error: " << err << std::endl;
        sqlite3_free(err);
    }

    // WAL mode for better concurrent access with GUI
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    return db;
}

void recordTime(sqlite3 *db, const std::string &app, const std::string &date, int seconds) {
    if (app.empty() || seconds <= 0) return;

    const char *sql =
        "INSERT INTO screen_time (app_name, date, seconds) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(app_name, date) DO UPDATE SET seconds = seconds + ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, app.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, seconds);
    sqlite3_bind_int(stmt, 4, seconds);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ─── Get Current Date ───────────────────────────────────────────

std::string getCurrentDate() {
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", t);
    return std::string(buf);
}

// ─── Main Loop ──────────────────────────────────────────────────

int main() {
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    // Open X11 display
    Display *dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        std::cerr << "Cannot open X display. Is X11 running?" << std::endl;
        return 1;
    }

    // Open database
    sqlite3 *db = openDb();
    if (!db) {
        XCloseDisplay(dpy);
        return 1;
    }

    std::cout << "sysmon-tracker started (PID " << getpid() << ")" << std::endl;
    std::cout << "DB: " << getDbPath() << std::endl;
    std::cout << "Idle threshold: " << IDLE_THRESHOLD_MS / 1000 << "s" << std::endl;

    auto lastTick = std::chrono::steady_clock::now();
    std::string lastApp;
    int accumSeconds = 0;
    std::string lastDate = getCurrentDate();

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));

        auto now = std::chrono::steady_clock::now();
        int elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTick).count();
        lastTick = now;

        // Check if date changed (midnight rollover)
        std::string today = getCurrentDate();
        if (today != lastDate) {
            // Flush accumulated time for previous day
            if (!lastApp.empty() && accumSeconds > 0) {
                recordTime(db, lastApp, lastDate, accumSeconds);
                accumSeconds = 0;
            }
            lastDate = today;
        }

        // Check idle
        long idleMs = getIdleMs(dpy);
        if (idleMs > IDLE_THRESHOLD_MS) {
            // User is idle — flush and don't count
            if (!lastApp.empty() && accumSeconds > 0) {
                recordTime(db, lastApp, today, accumSeconds);
                accumSeconds = 0;
            }
            lastApp.clear();
            continue;
        }

        // Screen is locked? Check if screensaver is active
        // If idle is increasing but below threshold, user is still present

        // Get focused app
        std::string app = getFocusedApp(dpy);
        if (app.empty()) {
            // No focused window (maybe screen locked or switching)
            if (!lastApp.empty() && accumSeconds > 0) {
                recordTime(db, lastApp, today, accumSeconds);
                accumSeconds = 0;
            }
            lastApp.clear();
            continue;
        }

        // Same app as before?
        if (app == lastApp) {
            accumSeconds += elapsed;
            // Flush every 30 seconds to keep DB relatively current
            if (accumSeconds >= 30) {
                recordTime(db, app, today, accumSeconds);
                accumSeconds = 0;
            }
        } else {
            // App changed — flush old, start new
            if (!lastApp.empty() && accumSeconds > 0) {
                recordTime(db, lastApp, today, accumSeconds);
            }
            lastApp = app;
            accumSeconds = elapsed;
        }
    }

    // Final flush
    if (!lastApp.empty() && accumSeconds > 0) {
        recordTime(db, lastApp, getCurrentDate(), accumSeconds);
    }

    sqlite3_close(db);
    XCloseDisplay(dpy);

    std::cout << "sysmon-tracker stopped." << std::endl;
    return 0;
}