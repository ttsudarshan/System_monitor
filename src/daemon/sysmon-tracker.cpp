// sysmon-tracker: Background daemon for accurate screen time tracking
// Runs independently of the System Monitor GUI
// Records focused app + idle detection to SQLite
// Supports both X11 and Wayland (GNOME Shell Introspect) sessions

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
#include <dbus/dbus.h>

#include <sqlite3.h>
#include <vector>

// ─── Globals ────────────────────────────────────────────────────

static volatile bool running = true;
static const int TICK_MS = 1000;

void sigHandler(int) { running = false; }

// ─── Session Type ────────────────────────────────────────────────

enum class SessionType { X11, WAYLAND };

static SessionType detectSessionType() {
    const char *wayland = getenv("WAYLAND_DISPLAY");
    if (wayland && wayland[0]) return SessionType::WAYLAND;
    const char *display = getenv("DISPLAY");
    if (display && display[0]) return SessionType::X11;
    return SessionType::WAYLAND; // Default to Wayland on modern systems
}

// ─── App Name Normalization ─────────────────────────────────────

std::string normalize(const std::string &name) {
    std::string n = name;
    for (auto &c : n) c = tolower(c);

    if (n.find("brave") != std::string::npos) return "brave";
    if (n.find("firefox") != std::string::npos) return "firefox";
    if (n.find("chromium") != std::string::npos) return "chromium";
    if (n.find("chrome") != std::string::npos) return "google-chrome";
    if (n == "code" || n.find("vscode") != std::string::npos ||
        n.find("code-oss") != std::string::npos) return "vscode";
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
    if (n.find("gnome-terminal") != std::string::npos ||
        n.find("gnome.terminal") != std::string::npos) return "terminal";
    if (n.find("xterm") != std::string::npos) return "terminal";
    if (n.find("konsole") != std::string::npos) return "terminal";
    if (n.find("tilix") != std::string::npos) return "terminal";
    if (n.find("alacritty") != std::string::npos) return "terminal";
    if (n.find("kitty") != std::string::npos) return "terminal";
    if (n.find("gnome-system") != std::string::npos) return "gnome-system-monitor";
    if (n.find("systemmonitor") != std::string::npos) return "system monitor";
    if (n.find("microsoft-edge") != std::string::npos ||
        n.find("msedge") != std::string::npos ||
        (n.find("microsoft") != std::string::npos && n.find("edge") != std::string::npos))
        return "microsoft-edge";
    if (n.find("obs") != std::string::npos) return "obs-studio";
    if (n.find("zoom") != std::string::npos) return "zoom";
    if (n.find("teams") != std::string::npos) return "teams";
    return n; // Return lowercased for unknown apps
}

// Wayland app IDs are reverse-domain (e.g. "org.mozilla.firefox", "com.brave.Browser")
// Try substring match on the full ID first, then fall back to the last segment.
std::string normalizeWaylandAppId(const std::string &appId) {
    std::string result = normalize(appId);
    // If normalize matched something specific it returns a known name;
    // if it returned the lowercased appId unchanged, extract the last segment.
    std::string lower = appId;
    for (auto &c : lower) c = tolower(c);
    if (result == lower) {
        size_t dot = appId.rfind('.');
        if (dot != std::string::npos) {
            std::string seg = appId.substr(dot + 1);
            for (auto &c : seg) c = tolower(c);
            if (!seg.empty()) return seg;
        }
    }
    return result;
}

// ─── X11: Get Focused App + Window Title ───────────────────────

std::string getFocusedApp(Display *dpy, std::string &outTitle) {
    outTitle.clear();
    Window focused;
    int revert;
    XGetInputFocus(dpy, &focused, &revert);

    if (focused == 0 || focused == 1) return "";

    Atom wmClass   = XInternAtom(dpy, "WM_CLASS", 1);
    Atom netWmName = XInternAtom(dpy, "_NET_WM_NAME", 1);
    Atom utf8Str   = XInternAtom(dpy, "UTF8_STRING", 1);
    if (wmClass == 0) return "";

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
            std::string instance(reinterpret_cast<char*>(prop));
            std::string className;
            if (nItems > instance.length() + 1)
                className = std::string(reinterpret_cast<char*>(prop) + instance.length() + 1);
            XFree(prop);

            std::string appName = normalize(className.empty() ? instance : className);

            if (netWmName && utf8Str) {
                unsigned char *tp = nullptr;
                Atom at; int af; unsigned long ni, ba;
                if (XGetWindowProperty(dpy, current, netWmName, 0, 4096, 0,
                                       utf8Str, &at, &af, &ni, &ba, &tp) == Success && tp) {
                    outTitle = reinterpret_cast<char*>(tp);
                    XFree(tp);
                }
            }

            return appName;
        }

        if (!XQueryTree(dpy, current, &root, &parent, &children, &nChildren)) break;
        if (children) XFree(children);
        if (parent == root || parent == current) break;
        current = parent;
    }

    return "";
}

// ─── X11: Screen Blanked Check ──────────────────────────────────

bool isScreenBlanked(Display *dpy) {
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

// ─── D-Bus: Persistent Session Bus Connection ───────────────────

static DBusConnection* getDbusConn() {
    static DBusConnection *conn = nullptr;
    if (!conn) {
        DBusError err;
        dbus_error_init(&err);
        conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
        if (dbus_error_is_set(&err)) {
            std::cerr << "D-Bus connection error: " << err.message << std::endl;
            dbus_error_free(&err);
            conn = nullptr;
        }
    }
    return conn;
}

// ─── Wayland: Screen Locked Check via D-Bus ─────────────────────
// Uses org.freedesktop.ScreenSaver (works on GNOME and KDE)

bool isScreenBlankedWayland() {
    DBusConnection *conn = getDbusConn();
    if (!conn) return false;

    DBusError err;
    dbus_error_init(&err);

    DBusMessage *msg = dbus_message_new_method_call(
        "org.freedesktop.ScreenSaver",
        "/org/freedesktop/ScreenSaver",
        "org.freedesktop.ScreenSaver",
        "GetActive"
    );
    if (!msg) return false;

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(conn, msg, 300, &err);
    dbus_message_unref(msg);

    if (!reply || dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        return false; // Can't determine → assume screen is on
    }

    dbus_bool_t active = false;
    DBusMessageIter iter;
    dbus_message_iter_init(reply, &iter);
    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_BOOLEAN)
        dbus_message_iter_get_basic(&iter, &active);

    dbus_message_unref(reply);
    return (bool)active;
}

// ─── Wayland: Focused Window via GNOME Shell Introspect ─────────
// Uses org.gnome.Shell.Introspect GetWindows() → a{ta{sv}}
// Each window dict has keys: "app-id" (string), "title" (string), "in-focus" (bool)

static bool gnomeIntrospectAvailable = true;
static int  gnomeIntrospectFailures  = 0;

struct WaylandFocusInfo { std::string appId, title; };

bool getFocusedWindowWayland(WaylandFocusInfo &info) {
    if (!gnomeIntrospectAvailable) return false;

    DBusConnection *conn = getDbusConn();
    if (!conn) return false;

    DBusError err;
    dbus_error_init(&err);

    DBusMessage *msg = dbus_message_new_method_call(
        "org.gnome.Shell",
        "/org/gnome/Shell/Introspect",
        "org.gnome.Shell.Introspect",
        "GetWindows"
    );
    if (!msg) return false;

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(conn, msg, 500, &err);
    dbus_message_unref(msg);

    if (!reply || dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        if (++gnomeIntrospectFailures >= 3) {
            gnomeIntrospectAvailable = false;
            std::cerr << "GNOME Shell Introspect unavailable — window tracking disabled on this compositor." << std::endl;
            std::cerr << "Only GNOME Wayland is supported. On KDE or other compositors, use an X11 session." << std::endl;
        }
        return false;
    }
    gnomeIntrospectFailures = 0;

    // Parse a{ta{sv}}
    DBusMessageIter iter;
    dbus_message_iter_init(reply, &iter);
    bool found = false;

    if (dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_ARRAY) {
        DBusMessageIter outer;
        dbus_message_iter_recurse(&iter, &outer);

        while (!found && dbus_message_iter_get_arg_type(&outer) == DBUS_TYPE_DICT_ENTRY) {
            DBusMessageIter entry;
            dbus_message_iter_recurse(&outer, &entry);

            // Skip uint64 window ID key
            dbus_message_iter_next(&entry);

            // Parse inner a{sv} (window properties)
            if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_ARRAY) {
                DBusMessageIter inner;
                dbus_message_iter_recurse(&entry, &inner);

                std::string appId, title;
                bool inFocus = false;

                while (dbus_message_iter_get_arg_type(&inner) == DBUS_TYPE_DICT_ENTRY) {
                    DBusMessageIter kv;
                    dbus_message_iter_recurse(&inner, &kv);

                    const char *key = nullptr;
                    dbus_message_iter_get_basic(&kv, &key);
                    dbus_message_iter_next(&kv);

                    if (dbus_message_iter_get_arg_type(&kv) == DBUS_TYPE_VARIANT) {
                        DBusMessageIter var;
                        dbus_message_iter_recurse(&kv, &var);

                        if (key && strcmp(key, "app-id") == 0 &&
                            dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                            const char *val;
                            dbus_message_iter_get_basic(&var, &val);
                            appId = val ? val : "";
                        } else if (key && strcmp(key, "title") == 0 &&
                                   dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING) {
                            const char *val;
                            dbus_message_iter_get_basic(&var, &val);
                            title = val ? val : "";
                        } else if (key && strcmp(key, "in-focus") == 0 &&
                                   dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_BOOLEAN) {
                            dbus_bool_t val;
                            dbus_message_iter_get_basic(&var, &val);
                            inFocus = (bool)val;
                        }
                    }

                    dbus_message_iter_next(&inner);
                }

                if (inFocus) {
                    info.appId = appId;
                    info.title = title;
                    found = true;
                }
            }

            dbus_message_iter_next(&outer);
        }
    }

    dbus_message_unref(reply);
    return found;
}

// ─── Browser Tab Helpers ─────────────────────────────────────────

bool isBrowserApp(const std::string &app) {
    return app == "google-chrome" || app == "chromium" ||
           app == "brave"         || app == "firefox"  ||
           app == "microsoft-edge";
}

std::string extractSiteFromTitle(const std::string &title, const std::string &browser) {
    std::string t = title;

    auto stripSuffix = [&](const std::string &suffix) {
        size_t pos = t.rfind(suffix);
        if (pos != std::string::npos) t = t.substr(0, pos);
    };

    if      (browser == "google-chrome")  { stripSuffix(" - Google Chrome");  stripSuffix(" \xe2\x80\x94 Google Chrome"); }
    else if (browser == "chromium")       { stripSuffix(" - Chromium"); }
    else if (browser == "brave")          { stripSuffix(" - Brave"); }
    else if (browser == "firefox")        { stripSuffix(" \xe2\x80\x94 Mozilla Firefox"); stripSuffix(" - Mozilla Firefox"); }
    else if (browser == "microsoft-edge") { stripSuffix(" - Microsoft Edge"); }

    while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t = t.substr(1);

    if (t.empty() || t == "New Tab" || t == "about:blank" || t == "New tab") return "";

    std::string tl = t;
    for (auto &c : tl) c = tolower(c);

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

    auto trimSeg = [](const std::string &s) -> std::string {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    };

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
        if (t.size() > 60) t = t.substr(0, 57) + "...";
        return t;
    }

    {
        size_t pos = t.rfind('|');
        if (pos != std::string::npos) {
            std::string seg = trimSeg(t.substr(pos + 1));
            if (!seg.empty() && seg.size() <= 40)
                return seg;
        }
    }

    std::string shortest;
    for (const auto &seg : segments) {
        if (shortest.empty() || seg.size() < shortest.size())
            shortest = seg;
    }
    if (!shortest.empty() && shortest.size() <= 35)
        return shortest;

    std::string first = segments.front();
    if (first.size() > 60) first = first.substr(0, 57) + "...";
    return first;
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

    const char *sql =
        "CREATE TABLE IF NOT EXISTS screen_time ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  app_name TEXT NOT NULL,"
        "  date TEXT NOT NULL,"
        "  seconds INTEGER NOT NULL DEFAULT 0,"
        "  UNIQUE(app_name, date)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_st_date ON screen_time(date);"
        "CREATE TABLE IF NOT EXISTS browser_tab_time ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  browser TEXT NOT NULL,"
        "  site TEXT NOT NULL,"
        "  date TEXT NOT NULL,"
        "  seconds INTEGER NOT NULL DEFAULT 0,"
        "  UNIQUE(browser, site, date)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_btt_date ON browser_tab_time(date);";

    char *err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) { std::cerr << "SQL error: " << err << std::endl; sqlite3_free(err); }

    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    return db;
}

void recordBrowserTab(sqlite3 *db, const std::string &browser, const std::string &site,
                      const std::string &date, int seconds) {
    if (browser.empty() || site.empty() || seconds <= 0) return;

    const char *sql =
        "INSERT INTO browser_tab_time (browser, site, date, seconds) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(browser, site, date) DO UPDATE SET seconds = seconds + ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, browser.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, site.c_str(),    -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, date.c_str(),    -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt,  4, seconds);
    sqlite3_bind_int(stmt,  5, seconds);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void recordTime(sqlite3 *db, const std::string &app, const std::string &date, int seconds) {
    if (app.empty() || seconds <= 0) return;

    const char *sql =
        "INSERT INTO screen_time (app_name, date, seconds) "
        "VALUES (?, ?, ?) "
        "ON CONFLICT(app_name, date) DO UPDATE SET seconds = seconds + ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, app.c_str(),  -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt,  3, seconds);
    sqlite3_bind_int(stmt,  4, seconds);
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

    SessionType session = detectSessionType();

    Display *dpy = nullptr;
    if (session == SessionType::X11) {
        dpy = XOpenDisplay(nullptr);
        if (!dpy) {
            std::cerr << "Cannot open X display, falling back to Wayland mode." << std::endl;
            session = SessionType::WAYLAND;
        }
    }

    if (session == SessionType::WAYLAND) {
        if (!getDbusConn()) {
            std::cerr << "Cannot connect to D-Bus session bus. Is a graphical session running?" << std::endl;
            return 1;
        }
    }

    sqlite3 *db = openDb();
    if (!db) {
        if (dpy) XCloseDisplay(dpy);
        return 1;
    }

    std::cout << "sysmon-tracker started (PID " << getpid() << ")" << std::endl;
    std::cout << "Session: " << (session == SessionType::WAYLAND ? "Wayland" : "X11") << std::endl;
    std::cout << "DB: " << getDbPath() << std::endl;

    auto lastTick = std::chrono::steady_clock::now();
    std::string lastApp, lastBrowser, lastSite;
    int accumSeconds = 0, accumTabSeconds = 0;
    std::string lastDate = getCurrentDate();

    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(TICK_MS));

        auto now = std::chrono::steady_clock::now();
        int elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTick).count();
        lastTick = now;

        std::string today = getCurrentDate();
        if (today != lastDate) {
            if (!lastApp.empty()  && accumSeconds    > 0) recordTime(db, lastApp, lastDate, accumSeconds);
            if (!lastSite.empty() && accumTabSeconds > 0) recordBrowserTab(db, lastBrowser, lastSite, lastDate, accumTabSeconds);
            accumSeconds = accumTabSeconds = 0;
            lastDate = today;
        }

        // ── Screen blanked check ──
        bool blanked = (session == SessionType::WAYLAND)
            ? isScreenBlankedWayland()
            : isScreenBlanked(dpy);

        if (blanked) {
            if (!lastApp.empty()  && accumSeconds    > 0) recordTime(db, lastApp, today, accumSeconds);
            if (!lastSite.empty() && accumTabSeconds > 0) recordBrowserTab(db, lastBrowser, lastSite, today, accumTabSeconds);
            accumSeconds = accumTabSeconds = 0;
            lastApp.clear(); lastSite.clear(); lastBrowser.clear();
            continue;
        }

        // ── Get focused app + title ──
        std::string app, windowTitle;

        if (session == SessionType::WAYLAND) {
            WaylandFocusInfo winfo;
            if (getFocusedWindowWayland(winfo)) {
                app         = normalizeWaylandAppId(winfo.appId);
                windowTitle = winfo.title;
            }
        } else {
            app = getFocusedApp(dpy, windowTitle);
        }

        if (app.empty()) {
            if (!lastApp.empty()  && accumSeconds    > 0) recordTime(db, lastApp, today, accumSeconds);
            if (!lastSite.empty() && accumTabSeconds > 0) recordBrowserTab(db, lastBrowser, lastSite, today, accumTabSeconds);
            accumSeconds = accumTabSeconds = 0;
            lastApp.clear(); lastSite.clear(); lastBrowser.clear();
            continue;
        }

        // ── App-level tracking ──
        if (app == lastApp) {
            accumSeconds += elapsed;
            if (accumSeconds >= 30) { recordTime(db, app, today, accumSeconds); accumSeconds = 0; }
        } else {
            if (!lastApp.empty() && accumSeconds > 0) recordTime(db, lastApp, today, accumSeconds);
            lastApp = app;
            accumSeconds = elapsed;
        }

        // ── Browser tab tracking ──
        std::string site;
        if (isBrowserApp(app) && !windowTitle.empty())
            site = extractSiteFromTitle(windowTitle, app);

        bool tabChanged = (site != lastSite || app != lastBrowser);
        if (tabChanged && !lastSite.empty() && accumTabSeconds > 0) {
            recordBrowserTab(db, lastBrowser, lastSite, today, accumTabSeconds);
            accumTabSeconds = 0;
            lastSite.clear(); lastBrowser.clear();
        }

        if (!site.empty()) {
            if (!tabChanged) {
                accumTabSeconds += elapsed;
                if (accumTabSeconds >= 30) {
                    recordBrowserTab(db, app, site, today, accumTabSeconds);
                    accumTabSeconds = 0;
                }
            } else {
                lastSite    = site;
                lastBrowser = app;
                accumTabSeconds = elapsed;
            }
        }
    }

    // Final flush
    if (!lastApp.empty()  && accumSeconds    > 0) recordTime(db, lastApp, getCurrentDate(), accumSeconds);
    if (!lastSite.empty() && accumTabSeconds > 0) recordBrowserTab(db, lastBrowser, lastSite, getCurrentDate(), accumTabSeconds);

    sqlite3_close(db);
    if (dpy) XCloseDisplay(dpy);

    std::cout << "sysmon-tracker stopped." << std::endl;
    return 0;
}
