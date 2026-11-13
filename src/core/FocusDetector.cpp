#include "FocusDetector.h"
#include "AppNames.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/scrnsaver.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
using std::chrono::seconds;

std::function<void()> FocusDetector::onDisplayLost;

namespace {

std::string runCommand(const std::string &cmd) {
    std::string result;
    FILE *fp = popen(cmd.c_str(), "r");
    if (!fp) return result;
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) result += buf;
    pclose(fp);
    while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back()))) result.pop_back();
    return result;
}

std::string lower(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && static_cast<unsigned char>(s.back()) <= ' ') s.pop_back();
    size_t b = 0;
    while (b < s.size() && static_cast<unsigned char>(s[b]) <= ' ') ++b;
    return s.substr(b);
}

bool endsWith(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Case-insensitive whole-word search ("youtube" matches "YouTube Music" but
// "bing" does not match "climbing").
bool wordMatch(const std::string &lowerHaystack, const char *needle) {
    size_t n = strlen(needle);
    size_t pos = lowerHaystack.find(needle);
    while (pos != std::string::npos) {
        bool before = pos == 0 || !std::isalnum(static_cast<unsigned char>(lowerHaystack[pos - 1]));
        bool after = pos + n >= lowerHaystack.size()
                  || !std::isalnum(static_cast<unsigned char>(lowerHaystack[pos + n]));
        if (before && after) return true;
        pos = lowerHaystack.find(needle, pos + 1);
    }
    return false;
}

int xErrorHandler(Display *, XErrorEvent *) {
    // A window can vanish between two requests; that is not fatal. The Xlib
    // default handler would exit the process.
    return 0;
}

int xIOErrorHandler(Display *) {
    fprintf(stderr, "X connection lost, exiting so the service manager can restart us\n");
    if (FocusDetector::onDisplayLost) FocusDetector::onDisplayLost();
    _exit(0);
}

bool commandExists(const char *name) {
    std::string out = runCommand(std::string("command -v ") + name + " 2>/dev/null");
    return !out.empty();
}

} // namespace

// ─── Lifecycle ──────────────────────────────────────────────────

void FocusDetector::initThreads() { XInitThreads(); }

FocusDetector::FocusDetector() {
    XSetErrorHandler(xErrorHandler);
    XSetIOErrorHandler(xIOErrorHandler);

    const char *st = getenv("XDG_SESSION_TYPE");
    sessionIsWayland_ = (st && strcmp(st, "wayland") == 0) || (!st && getenv("WAYLAND_DISPLAY"));
    const char *d = getenv("XDG_CURRENT_DESKTOP");
    desktop_ = d ? lower(d) : "";
    isGnome_ = desktop_.find("gnome") != std::string::npos || desktop_.find("ubuntu") != std::string::npos;

    haveGdbus_ = commandExists("gdbus");
    haveBusctl_ = commandExists("busctl");

    ensureDisplay();
}

FocusDetector::~FocusDetector() {
    if (dpy_) XCloseDisplay(dpy_);
}

const char *FocusDetector::sourceName(Source s) {
    switch (s) {
        case Source::X11: return "X11";
        case Source::GnomeExtension: return "GNOME extension";
        case Source::Heuristic: return "CPU heuristic";
        default: return "none";
    }
}

// ─── X connection ───────────────────────────────────────────────

void FocusDetector::importSessionEnv() {
    // When started by systemd --user before the desktop session, DISPLAY and
    // XAUTHORITY are missing or stale. The session imports the real values
    // into the user manager after login, so pick them up from there.
    std::string out = runCommand("systemctl --user show-environment 2>/dev/null");
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (val.empty()) continue;
        if (key == "DISPLAY" || key == "XAUTHORITY" || key == "WAYLAND_DISPLAY"
            || key == "XDG_SESSION_TYPE" || key == "XDG_CURRENT_DESKTOP"
            || key == "DBUS_SESSION_BUS_ADDRESS")
            setenv(key.c_str(), val.c_str(), 1);
    }
    const char *st = getenv("XDG_SESSION_TYPE");
    if (st) sessionIsWayland_ = strcmp(st, "wayland") == 0;
    const char *d = getenv("XDG_CURRENT_DESKTOP");
    if (d) {
        desktop_ = lower(d);
        isGnome_ = desktop_.find("gnome") != std::string::npos || desktop_.find("ubuntu") != std::string::npos;
    }
}

void FocusDetector::ensureDisplay() {
    if (dpy_) return;
    auto now = Clock::now();
    int backoff = connectFailures_ < 4 ? 15 : 60;
    if (lastConnectAttempt_.time_since_epoch().count() != 0 && now - lastConnectAttempt_ < seconds(backoff))
        return;
    lastConnectAttempt_ = now;

    if (connectFailures_ > 0 || !getenv("DISPLAY")) importSessionEnv();
    const char *disp = getenv("DISPLAY");
    if (!disp || !*disp) { ++connectFailures_; return; }

    dpy_ = XOpenDisplay(nullptr);
    if (!dpy_) { ++connectFailures_; return; }

    atomActiveWindow_ = XInternAtom(dpy_, "_NET_ACTIVE_WINDOW", False);
    atomWmClass_ = XInternAtom(dpy_, "WM_CLASS", False);
    atomNetWmName_ = XInternAtom(dpy_, "_NET_WM_NAME", False);
    atomUtf8_ = XInternAtom(dpy_, "UTF8_STRING", False);
    atomWmName_ = XInternAtom(dpy_, "WM_NAME", False);
    connectFailures_ = 0;
    printf("Connected to X display %s\n", disp);
    fflush(stdout);
}

std::string FocusDetector::readProperty(unsigned long window, unsigned long atom,
                                        unsigned long type, bool *okOut) {
    *okOut = false;
    Atom actualType;
    int actualFormat = 0;
    unsigned long nItems = 0, bytesAfter = 0;
    unsigned char *prop = nullptr;
    std::string value;
    if (XGetWindowProperty(dpy_, window, atom, 0, 4096, False, type, &actualType,
                           &actualFormat, &nItems, &bytesAfter, &prop) == Success && prop) {
        if (actualFormat == 8 && nItems > 0) {
            value.assign(reinterpret_cast<char *>(prop), nItems);
            *okOut = true;
        }
        XFree(prop);
    }
    return value;
}

// ─── Screen state ───────────────────────────────────────────────

bool FocusDetector::screenOffX11() {
    if (!dpy_) return false;
    int ev = 0, err = 0;

    // DPMS is what every modern desktop uses to turn the panel off.
    if (DPMSQueryExtension(dpy_, &ev, &err)) {
        CARD16 level = DPMSModeOn;
        BOOL enabled = False;
        if (DPMSInfo(dpy_, &level, &enabled) && enabled && level != DPMSModeOn) return true;
    }

    // Legacy X screen saver (xset s).
    if (XScreenSaverQueryExtension(dpy_, &ev, &err)) {
        XScreenSaverInfo *info = XScreenSaverAllocInfo();
        if (info) {
            XScreenSaverQueryInfo(dpy_, DefaultRootWindow(dpy_), info);
            int state = info->state;
            XFree(info);
            if (state == ScreenSaverOn || state == ScreenSaverCycle) return true;
        }
    }
    return false;
}

bool FocusDetector::screenLockedDbus() {
    auto now = Clock::now();
    if (lastLockCheck_.time_since_epoch().count() != 0 && now - lastLockCheck_ < seconds(5))
        return lockedCached_;
    lastLockCheck_ = now;
    if (!haveGdbus_ && !haveBusctl_) return false;

    struct Backend { const char *dest, *path, *iface; const char *desktopHint; };
    static const Backend backends[] = {
        {"org.cinnamon.ScreenSaver", "/org/cinnamon/ScreenSaver", "org.cinnamon.ScreenSaver", "cinnamon"},
        {"org.gnome.ScreenSaver", "/org/gnome/ScreenSaver", "org.gnome.ScreenSaver", "gnome"},
        {"org.mate.ScreenSaver", "/org/mate/ScreenSaver", "org.mate.ScreenSaver", "mate"},
        {"org.xfce.ScreenSaver", "/org/xfce/ScreenSaver", "org.xfce.ScreenSaver", "xfce"},
        {"org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", "kde"},
    };
    const int count = static_cast<int>(sizeof(backends) / sizeof(backends[0]));

    auto query = [&](int i, bool &answered) -> bool {
        const Backend &b = backends[i];
        std::string out;
        if (haveGdbus_) {
            out = runCommand(std::string("gdbus call --session --dest ") + b.dest
                             + " --object-path " + b.path + " --method " + b.iface
                             + ".GetActive 2>/dev/null");
        } else {
            out = runCommand(std::string("busctl --user call ") + b.dest + " " + b.path
                             + " " + b.iface + " GetActive 2>/dev/null");
        }
        if (out.find("true") != std::string::npos) { answered = true; return true; }
        if (out.find("false") != std::string::npos) { answered = true; return false; }
        answered = false;
        return false;
    };

    bool answered = false;
    if (lockBackend_ >= 0) {
        bool locked = query(lockBackend_, answered);
        if (answered) { lockedCached_ = locked; return locked; }
    }

    // Try the backend matching the desktop first, then the rest.
    std::vector<int> order;
    for (int i = 0; i < count; ++i)
        if (desktop_.find(backends[i].desktopHint) != std::string::npos) order.push_back(i);
    for (int i = 0; i < count; ++i)
        if (std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);

    for (int i : order) {
        bool locked = query(i, answered);
        if (answered) {
            lockBackend_ = i;
            lockedCached_ = locked;
            return locked;
        }
    }
    // No screensaver service answered; do not spawn five processes every 5 s.
    lockBackend_ = -1;
    lockedCached_ = false;
    lastLockCheck_ = now + seconds(55);
    return false;
}

// ─── Focus detection paths ──────────────────────────────────────

std::string FocusDetector::x11App(std::string &titleOut) {
    titleOut.clear();
    if (!dpy_) return "";

    Window root = DefaultRootWindow(dpy_);
    Window active = 0;
    {
        Atom at; int af; unsigned long n, ba; unsigned char *p = nullptr;
        if (XGetWindowProperty(dpy_, root, atomActiveWindow_, 0, 1, False, XA_WINDOW,
                               &at, &af, &n, &ba, &p) == Success && p) {
            if (n > 0) active = *reinterpret_cast<Window *>(p);
            XFree(p);
        }
    }
    if (active == 0) {
        int revert;
        XGetInputFocus(dpy_, &active, &revert);
    }
    if (active <= 1) return "";  // None / PointerRoot

    Window current = active;
    for (int depth = 0; depth < 20; ++depth) {
        bool ok = false;
        std::string cls = readProperty(current, atomWmClass_, XA_STRING, &ok);
        if (ok) {
            // WM_CLASS is "instance\0class\0"
            std::string instance(cls.c_str());
            std::string className;
            if (cls.size() > instance.size() + 1) className = std::string(cls.c_str() + instance.size() + 1);
            std::string chosen = className.empty() ? instance : className;

            std::string l = lower(chosen);
            static const char *const LOCKERS[] = {"screensaver", "i3lock", "swaylock", "light-locker",
                                                  "xsecurelock", "gtklock", "slock", "lockscreen",
                                                  "xscreensaver", nullptr};
            for (int i = 0; LOCKERS[i]; ++i)
                if (l.find(LOCKERS[i]) != std::string::npos) return "";  // lock screen has focus

            titleOut = readProperty(current, atomNetWmName_, atomUtf8_, &ok);
            if (!ok) titleOut = readProperty(current, atomWmName_, AnyPropertyType, &ok);
            return AppNames::canonical(chosen);
        }
        Window parent = 0, qroot = 0, *children = nullptr;
        unsigned int nChildren = 0;
        if (!XQueryTree(dpy_, current, &qroot, &parent, &children, &nChildren)) break;
        if (children) XFree(children);
        if (parent == 0 || parent == qroot || parent == current) break;
        current = parent;
    }
    return "";
}

std::string FocusDetector::gnomeExtensionApp() {
    auto now = Clock::now();
    if (lastExtFailure_.time_since_epoch().count() != 0 && now - lastExtFailure_ < seconds(60))
        return "";
    std::string out = runCommand(
        "gdbus call --session --dest org.sysmon.Tracker --object-path /org/sysmon/Tracker "
        "--method org.sysmon.Tracker.GetActiveApp 2>/dev/null");
    size_t start = out.find('\'');
    if (start != std::string::npos) {
        size_t end = out.find('\'', start + 1);
        if (end != std::string::npos && end > start + 1)
            return AppNames::canonical(out.substr(start + 1, end - start - 1));
    }
    lastExtFailure_ = now;
    return "";
}

std::string FocusDetector::heuristicApp() {
    auto now = Clock::now();
    int self = getpid();

    if (candidates_.empty() || now - lastCandidateScan_ > seconds(15)) {
        lastCandidateScan_ = now;
        candidates_.clear();
        std::unordered_map<int, bool> alive;

        DIR *dir = opendir("/proc");
        if (!dir) return "";
        struct dirent *ent;
        while ((ent = readdir(dir))) {
            char *endp = nullptr;
            long pid = strtol(ent->d_name, &endp, 10);
            if (*endp != '\0' || pid <= 100 || pid == self) continue;

            auto cached = guiPidCache_.find(static_cast<int>(pid));
            bool isGui;
            if (cached != guiPidCache_.end()) {
                isGui = cached->second;
            } else {
                isGui = false;
                std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
                std::string line;
                while (std::getline(maps, line)) {
                    if (line.find("libwayland-client") != std::string::npos
                        || line.find("libxcb.so") != std::string::npos) { isGui = true; break; }
                }
                if (isGui) {
                    std::ifstream c("/proc/" + std::to_string(pid) + "/comm");
                    std::string comm;
                    std::getline(c, comm);
                    if (AppNames::isDesktopNoise(comm)) isGui = false;
                }
                guiPidCache_[static_cast<int>(pid)] = isGui;
            }
            alive[static_cast<int>(pid)] = isGui;
            if (isGui) candidates_.push_back(static_cast<int>(pid));
        }
        closedir(dir);
        guiPidCache_ = std::move(alive);
        for (auto it = lastCpu_.begin(); it != lastCpu_.end();)
            it = guiPidCache_.count(it->first) ? std::next(it) : lastCpu_.erase(it);
    }

    int winner = -1;
    unsigned long long maxDelta = 0;
    for (int pid : candidates_) {
        std::ifstream s("/proc/" + std::to_string(pid) + "/stat");
        std::string line;
        if (!std::getline(s, line)) continue;
        size_t rp = line.rfind(')');
        if (rp == std::string::npos) continue;
        std::istringstream rest(line.substr(rp + 2));
        std::string tmp;
        for (int i = 0; i < 11; ++i) rest >> tmp;
        unsigned long long utime = 0, stime = 0;
        if (!(rest >> utime >> stime)) continue;
        unsigned long long total = utime + stime;
        auto prev = lastCpu_.find(pid);
        if (prev != lastCpu_.end() && total >= prev->second) {
            unsigned long long delta = total - prev->second;
            if (delta > maxDelta) { maxDelta = delta; winner = pid; }
        }
        lastCpu_[pid] = total;
    }

    if (winner < 0 || maxDelta == 0) return "";
    std::ifstream c("/proc/" + std::to_string(winner) + "/comm");
    std::string comm;
    if (!std::getline(c, comm) || comm.empty()) return "";
    return AppNames::canonical(comm);
}

// ─── Public entry point ─────────────────────────────────────────

FocusDetector::Result FocusDetector::poll() {
    ensureDisplay();
    Result r;

    if (screenOffX11() || screenLockedDbus()) {
        r.screenOff = true;
        return r;
    }

    if (isGnome_ && sessionIsWayland_) {
        r.app = gnomeExtensionApp();
        if (!r.app.empty()) { r.source = Source::GnomeExtension; return r; }
    }

    if (dpy_) {
        r.app = x11App(r.title);
        if (!r.app.empty()) {
            r.source = Source::X11;
            r.tab = extractTabName(r.app, r.title);
            return r;
        }
        if (!sessionIsWayland_) return r;  // X11 session: nothing focused, do not guess
    }

    // Wayland without a usable compositor hook (or an X11 session whose
    // display we could not reach yet): fall back to the heuristic only when
    // the session really is Wayland. Guessing on X11 produces garbage.
    if (sessionIsWayland_) {
        r.app = heuristicApp();
        if (!r.app.empty()) r.source = Source::Heuristic;
    }
    return r;
}

// ─── Browser tab names ──────────────────────────────────────────

std::string FocusDetector::extractTabName(const std::string &app, const std::string &titleIn) {
    if (!AppNames::isBrowser(app) || titleIn.empty()) return "";

    // Drop zero-width characters some pages put in titles (Edge's new tab page).
    std::string t;
    for (size_t i = 0; i < titleIn.size();) {
        unsigned char c = titleIn[i];
        if (c == 0xE2 && i + 2 < titleIn.size() && titleIn[i + 1] == '\x80'
            && (titleIn[i + 2] == '\x8B' || titleIn[i + 2] == '\x8C' || titleIn[i + 2] == '\x8D')) { i += 3; continue; }
        if (c == 0xE2 && i + 2 < titleIn.size() && titleIn[i + 1] == '\x81' && titleIn[i + 2] == '\xA0') { i += 3; continue; }
        if (c == 0xEF && i + 2 < titleIn.size() && titleIn[i + 1] == '\xBB' && titleIn[i + 2] == '\xBF') { i += 3; continue; }
        t += titleIn[i];
        ++i;
    }

    // Strip the trailing " - <Browser name>[ (Private)]" part.
    static const char *const BROWSER_NAMES[] = {
        "mozilla firefox", "firefox", "google chrome", "chromium", "microsoft edge",
        "brave", "vivaldi", "opera", "librewolf", nullptr};
    static const char *const SEPS[] = {" \xe2\x80\x94 ", " - ", " \xe2\x80\x93 ", nullptr};  // em dash, hyphen, en dash
    for (int s = 0; SEPS[s]; ++s) {
        size_t pos = t.rfind(SEPS[s]);
        if (pos == std::string::npos) continue;
        std::string tail = lower(trim(t.substr(pos + strlen(SEPS[s]))));
        bool isBrowser = false;
        for (int i = 0; BROWSER_NAMES[i]; ++i)
            if (tail.rfind(BROWSER_NAMES[i], 0) == 0) { isBrowser = true; break; }
        if (isBrowser) { t = t.substr(0, pos); break; }
    }
    t = trim(t);
    if (t.empty()) return "";

    std::string lt = lower(t);
    if (lt == "new tab" || lt == "new private tab" || lt == "start page" || lt == "untitled"
        || lt == "new incognito tab" || lt == "private browsing")
        return "";
    for (int i = 0; BROWSER_NAMES[i]; ++i)
        if (lt == BROWSER_NAMES[i]) return "";

    // Well-known sites: recognise them anywhere in the title.
    static const struct { const char *needle; const char *name; } SITES[] = {
        {"youtube", "YouTube"}, {"github", "GitHub"}, {"gitlab", "GitLab"}, {"gmail", "Gmail"},
        {"linkedin", "LinkedIn"}, {"reddit", "Reddit"}, {"stack overflow", "Stack Overflow"},
        {"stack exchange", "Stack Exchange"}, {"wikipedia", "Wikipedia"}, {"chatgpt", "ChatGPT"},
        {"claude", "Claude"}, {"netflix", "Netflix"}, {"twitch", "Twitch"}, {"facebook", "Facebook"},
        {"instagram", "Instagram"}, {"whatsapp", "WhatsApp"}, {"google docs", "Google Docs"},
        {"google sheets", "Google Sheets"}, {"google slides", "Google Slides"},
        {"google drive", "Google Drive"}, {"google calendar", "Google Calendar"},
        {"google meet", "Google Meet"}, {"google maps", "Google Maps"}, {"google search", "Google"},
        {"notion", "Notion"}, {"figma", "Figma"}, {"slack", "Slack"}, {"discord", "Discord"},
        {"spotify", "Spotify"}, {"amazon", "Amazon"}, {"brightspace", "Brightspace"},
        {"outlook", "Outlook"}, {"microsoft teams", "Microsoft Teams"}, {"zoom", "Zoom"},
        {"udemy", "Udemy"}, {"coursera", "Coursera"}, {"leetcode", "LeetCode"},
        {"hackerrank", "HackerRank"}, {"jira", "Jira"}, {"trello", "Trello"}, {"quora", "Quora"},
        {"pinterest", "Pinterest"}, {"tiktok", "TikTok"}, {"ebay", "eBay"}, {"mdn web docs", "MDN Web Docs"},
        {"duckduckgo", "DuckDuckGo"}, {"prime video", "Prime Video"}, {"disney+", "Disney+"},
        {"hulu", "Hulu"}, {"canvas", "Canvas"}, {"moodle", "Moodle"}, {"replit", "Replit"},
        {"codepen", "CodePen"}, {"vercel", "Vercel"}, {"aws console", "AWS Console"},
        {"microsoft azure", "Azure"}, {"bitbucket", "Bitbucket"}, {"asana", "Asana"},
        {nullptr, nullptr}};
    for (int i = 0; SITES[i].needle; ++i)
        if (wordMatch(lt, SITES[i].needle)) return SITES[i].name;
    if (endsWith(t, " / X") || t == "X") return "X (Twitter)";

    // Otherwise take the rightmost short segment: "Page title · Site" / "Page | Site".
    auto lastShortSegment = [&](const std::string &sep) -> std::string {
        size_t pos = t.rfind(sep);
        if (pos == std::string::npos) return "";
        std::string seg = trim(t.substr(pos + sep.size()));
        return (!seg.empty() && seg.size() <= 35) ? seg : "";
    };
    std::string site = lastShortSegment(" \xc2\xb7 ");                     // " · "
    if (site.empty()) site = lastShortSegment(" | ");
    if (site.empty()) site = lastShortSegment(" \xe2\x80\x94 ");           // " — "
    if (site.empty()) site = lastShortSegment(" - ");
    if (!site.empty()) return site;

    if (t.size() > 50) t = t.substr(0, 47) + "...";
    return t;
}
