#include "sysmon-tracker-core.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <unordered_map>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/scrnsaver.h>

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

std::string executeCommand(const std::string &cmd) {
    std::string result;
    FILE *fp = popen(cmd.c_str(), "r");
    if (fp) {
        char buf[256];
        while (fgets(buf, sizeof(buf), fp)) result += buf;
        pclose(fp);
    }
    result.erase(result.find_last_not_of(" \n\r\t")+1);
    return result;
}

std::string getWaylandFocusedApp() {
    const char *desktop = getenv("XDG_CURRENT_DESKTOP");
    if (!desktop || std::string(desktop).find("GNOME") == std::string::npos) return "";

    std::string out = executeCommand(
        "gdbus call --session "
        "--dest org.sysmon.Tracker "
        "--object-path /org/sysmon/Tracker "
        "--method org.sysmon.Tracker.GetActiveApp "
        "2>/dev/null");

    if (out.length() > 2) {
        size_t start = out.find("'");
        if (start != std::string::npos) {
            size_t end = out.find("'", start + 1);
            if (end != std::string::npos && end > start + 1) {
                return normalize(out.substr(start + 1, end - start - 1));
            }
        }
    }
    return "";
}

bool isScreenBlanked(Display *dpy) {
    std::string idleOut = executeCommand("busctl --user call org.gnome.Mutter.IdleMonitor /org/gnome/Mutter/IdleMonitor/Core org.gnome.Mutter.IdleMonitor GetIdletime 2>/dev/null");
    if (idleOut.length() > 2) {
        std::stringstream ss(idleOut.substr(2));
        unsigned long idleMs;
        if (ss >> idleMs) {
            std::string state = executeCommand("gdbus call --session --dest org.gnome.ScreenSaver --object-path /org/gnome/ScreenSaver --method org.gnome.ScreenSaver.GetActive 2>/dev/null");
            if (state.find("true") != std::string::npos) return true;
        }
    }

    if (!dpy) return false;
    int eventBase, errorBase;
    if (!XScreenSaverQueryExtension(dpy, &eventBase, &errorBase))
        return false;

    XScreenSaverInfo *info = XScreenSaverAllocInfo();
    if (!info) return false;

    XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), info);
    int state = info->state;
    XFree(info);

    return (state == ScreenSaverOn || state == ScreenSaverCycle);
}

std::string getFocusedApp(Display *dpy) {
    std::string wayApp = getWaylandFocusedApp();
    if (!wayApp.empty()) return wayApp;

    if (dpy) {
        if (!isScreenBlanked(dpy)) {
            Window activeWin = 0;
            Atom netActiveAtom = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
            if (netActiveAtom != 0) {
                Atom actualType;
                int actualFormat;
                unsigned long nItems, bytesAfter;
                unsigned char *prop = nullptr;
                if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), netActiveAtom,
                                       0, 1, False, XA_WINDOW,
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
                Atom wmClass = XInternAtom(dpy, "WM_CLASS", False);
                Window current = activeWin;
                Window root = DefaultRootWindow(dpy);
                Window parent;
                Window *children;
                unsigned int nChildren;

                for (int depth = 0; depth < 20; ++depth) {
                    Atom actualType;
                    int actualFormat;
                    unsigned long nItems, bytesAfter;
                    unsigned char *prop = nullptr;

                    if (XGetWindowProperty(dpy, current, wmClass, 0, 1024, False,
                                           XA_STRING, &actualType, &actualFormat,
                                           &nItems, &bytesAfter, &prop) == Success && prop && nItems > 0) {
                        std::string instance(reinterpret_cast<char*>(prop));
                        std::string className;
                        if (nItems > instance.length() + 1)
                            className = std::string(reinterpret_cast<char*>(prop) + instance.length() + 1);
                        XFree(prop);
                        return normalize(className.empty() ? instance : className);
                    }
                    if (prop) XFree(prop);

                    if (!XQueryTree(dpy, current, &root, &parent, &children, &nChildren)) break;
                    if (children) XFree(children);
                    if (parent == root || parent == current) break;
                    current = parent;
                }
            }
        }
    }

    static std::unordered_map<int, unsigned long long> lastCpuTimes;
    int winnerPid = -1;
    unsigned long long maxDelta = 0;

    FILE *fp = popen("grep -l \"libwayland-client\\|libxcb\" /proc/*/maps 2>/dev/null | cut -d'/' -f3", "r");
    if (fp) {
        char buf[64];
        while (fgets(buf, sizeof(buf), fp)) {
            int pid = atoi(buf);
            if (pid <= 100) continue;

            std::string statFile = "/proc/" + std::to_string(pid) + "/stat";
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

    if (winnerPid > 0 && maxDelta > 0) {
        std::string commFile = "/proc/" + std::to_string(winnerPid) + "/comm";
        std::ifstream c(commFile);
        std::string name;
        if (std::getline(c, name) && !name.empty()) {
            if (name != "gnome-shell" && name != "mutter") {
                return normalize(name);
            }
        }
    }

    return "";
}

// Read the window title (_NET_WM_NAME) of the currently active X11 window.
// Returns empty string on Wayland-native or if no title is available.
std::string getWindowTitle(Display *dpy) {
    if (!dpy) return "";

    Window activeWin = 0;
    Atom netActive = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    {
        Atom at; int af; unsigned long n, ba; unsigned char *p = nullptr;
        if (XGetWindowProperty(dpy, DefaultRootWindow(dpy), netActive,
                               0, 1, False, XA_WINDOW, &at, &af, &n, &ba, &p) == Success && p) {
            activeWin = *reinterpret_cast<Window*>(p);
            XFree(p);
        }
    }
    if (activeWin <= 1) return "";

    // Prefer _NET_WM_NAME (UTF-8)
    Atom netWmName = XInternAtom(dpy, "_NET_WM_NAME", False);
    Atom utf8Type  = XInternAtom(dpy, "UTF8_STRING",  False);
    {
        Atom at; int af; unsigned long n, ba; unsigned char *p = nullptr;
        if (XGetWindowProperty(dpy, activeWin, netWmName, 0, 2048, False, utf8Type,
                               &at, &af, &n, &ba, &p) == Success && p && n > 0) {
            std::string title(reinterpret_cast<char*>(p));
            XFree(p);
            return title;
        }
        if (p) XFree(p);
    }

    // Fall back to legacy WM_NAME
    {
        Atom at; int af; unsigned long n, ba; unsigned char *p = nullptr;
        Atom wmName = XInternAtom(dpy, "WM_NAME", False);
        if (XGetWindowProperty(dpy, activeWin, wmName, 0, 2048, False, AnyPropertyType,
                               &at, &af, &n, &ba, &p) == Success && p && n > 0) {
            std::string title(reinterpret_cast<char*>(p));
            XFree(p);
            return title;
        }
        if (p) XFree(p);
    }
    return "";
}

static bool isBrowserApp(const std::string &app) {
    return app == "brave" || app == "firefox" || app == "google-chrome"
        || app == "chromium" || app == "microsoft-edge";
}

// Extract a stable "site name" from a browser window title.
// Browser titles follow: "Page Title [sep] Site Name - Browser Name"
// We strip the browser suffix then pull the last short segment as site name.
// Returns "" for non-browser apps.
std::string extractTabName(const std::string &app, const std::string &windowTitle) {
    if (!isBrowserApp(app) || windowTitle.empty()) return "";

    std::string t = windowTitle;

    // Strip known browser name suffixes (longest first to avoid partial matches)
    static const char* const SUFFIXES[] = {
        " \xe2\x80\x94 Mozilla Firefox",  // — Mozilla Firefox
        " - Mozilla Firefox",
        " \xe2\x80\x94 Firefox Nightly",
        " - Firefox Nightly",
        " - Google Chrome",
        " - Chromium",
        " - Microsoft Edge",
        " \xe2\x80\x94 Brave",            // — Brave
        " - Brave",
        " - Firefox",
        nullptr
    };
    for (int i = 0; SUFFIXES[i]; ++i) {
        const std::string sfx(SUFFIXES[i]);
        if (t.size() >= sfx.size() && t.compare(t.size() - sfx.size(), sfx.size(), sfx) == 0) {
            t.resize(t.size() - sfx.size());
            break;
        }
    }
    // Trim trailing whitespace
    while (!t.empty() && (unsigned char)t.back() <= ' ') t.pop_back();
    if (t.empty()) return "";

    // Extract the rightmost short segment as the site name.
    // Separators in priority order: · (U+00B7) > | > — (U+2014) > -
    // We accept a segment only if it is short (≤35 chars) — longer text is
    // likely a page description, not a site name.
    auto lastShortSeg = [&](const std::string &sep) -> std::string {
        size_t pos = t.rfind(sep);
        if (pos == std::string::npos) return "";
        std::string seg = t.substr(pos + sep.size());
        // Trim
        while (!seg.empty() && (unsigned char)seg.front() <= ' ') seg.erase(seg.begin());
        while (!seg.empty() && (unsigned char)seg.back()  <= ' ') seg.pop_back();
        return (seg.size() > 0 && seg.size() <= 35) ? seg : "";
    };

    std::string site;
    site = lastShortSeg(" \xc2\xb7 ");    // " · "  U+00B7, used by GitHub, Wikipedia
    if (site.empty()) site = lastShortSeg(" | ");
    if (site.empty()) site = lastShortSeg(" \xe2\x80\x94 ");  // " — "  U+2014
    if (site.empty()) site = lastShortSeg(" - ");

    if (!site.empty()) return site;

    // No separator found — the whole remaining string is the site name (e.g. "YouTube")
    if (t.size() > 50) t = t.substr(0, 47) + "...";
    return t;
}

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
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) return nullptr;

    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    // Detect whether the table exists and whether tab_name is present
    bool tableExists = false;
    bool hasTabName  = false;

    sqlite3_stmt *s = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT name FROM sqlite_master WHERE type='table' AND name='screen_time'",
            -1, &s, nullptr) == SQLITE_OK) {
        if (sqlite3_step(s) == SQLITE_ROW) tableExists = true;
        sqlite3_finalize(s);
    }

    if (tableExists) {
        if (sqlite3_prepare_v2(db, "PRAGMA table_info(screen_time)", -1, &s, nullptr) == SQLITE_OK) {
            while (sqlite3_step(s) == SQLITE_ROW) {
                const char *col = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
                if (col && strcmp(col, "tab_name") == 0) { hasTabName = true; break; }
            }
            sqlite3_finalize(s);
        }
    }

    if (!tableExists) {
        // Fresh install — create with new schema
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS screen_time ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  app_name TEXT NOT NULL,"
            "  tab_name TEXT NOT NULL DEFAULT '',"
            "  date TEXT NOT NULL,"
            "  seconds INTEGER NOT NULL DEFAULT 0,"
            "  UNIQUE(app_name, tab_name, date));"
            "CREATE INDEX IF NOT EXISTS idx_st_date ON screen_time(date);",
            nullptr, nullptr, nullptr);
    } else if (!hasTabName) {
        // Migrate: recreate table with tab_name column and updated UNIQUE constraint
        sqlite3_exec(db,
            "BEGIN;"
            "CREATE TABLE screen_time_new ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  app_name TEXT NOT NULL,"
            "  tab_name TEXT NOT NULL DEFAULT '',"
            "  date TEXT NOT NULL,"
            "  seconds INTEGER NOT NULL DEFAULT 0,"
            "  UNIQUE(app_name, tab_name, date));"
            "INSERT INTO screen_time_new(app_name, tab_name, date, seconds)"
            "  SELECT app_name, '', date, seconds FROM screen_time;"
            "DROP TABLE screen_time;"
            "ALTER TABLE screen_time_new RENAME TO screen_time;"
            "COMMIT;",
            nullptr, nullptr, nullptr);
        sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS idx_st_date ON screen_time(date);",
                     nullptr, nullptr, nullptr);
    }

    return db;
}

void recordTime(sqlite3 *db, const std::string &app, const std::string &tab,
                const std::string &date, int seconds) {
    if (app.empty() || seconds <= 0) return;

    const char *sql =
        "INSERT INTO screen_time (app_name, tab_name, date, seconds) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(app_name, tab_name, date) DO UPDATE SET seconds = seconds + ?;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

    sqlite3_bind_text(stmt, 1, app.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, tab.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int (stmt, 4, seconds);
    sqlite3_bind_int (stmt, 5, seconds);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::string getCurrentDate() {
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", t);
    return std::string(buf);
}
