#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct _XDisplay;
typedef struct _XDisplay Display;

// Figures out which application the user is looking at right now, and whether
// the screen is off / locked. Used by the daemon and, as a fallback, by the GUI.
//
// Detection paths, in priority order:
//   1. GNOME Shell companion extension over DBus   (GNOME on Wayland only)
//   2. X11 _NET_ACTIVE_WINDOW -> WM_CLASS           (any X11 or XWayland display)
//   3. "busiest GUI process" CPU heuristic           (Wayland sessions only)
//
// The X connection is (re)established lazily, so a daemon started before the
// X server is up recovers on its own.
class FocusDetector {
public:
    enum class Source { None, X11, GnomeExtension, Heuristic };

    struct Result {
        std::string app;      // canonical app name, "" if nothing is focused
        std::string tab;      // site name for browsers, "" otherwise
        std::string title;    // raw window title (X11 only)
        bool screenOff = false;
        Source source = Source::None;
    };

    FocusDetector();
    ~FocusDetector();
    FocusDetector(const FocusDetector &) = delete;
    FocusDetector &operator=(const FocusDetector &) = delete;

    Result poll();

    bool hasDisplay() const { return dpy_ != nullptr; }
    static const char *sourceName(Source s);

    // Extract a stable site name ("YouTube", "GitHub") from a browser window title.
    static std::string extractTabName(const std::string &app, const std::string &title);

    // Called (from the thread that hit the error) when the X connection dies.
    // The process exits afterwards; use it to flush state.
    static std::function<void()> onDisplayLost;

    // Must be called before any other Xlib use when several threads open displays.
    static void initThreads();

private:
    Display *dpy_ = nullptr;
    std::chrono::steady_clock::time_point lastConnectAttempt_{};
    int connectFailures_ = 0;

    bool sessionIsWayland_ = false;
    bool isGnome_ = false;
    std::string desktop_;

    // Cached X atoms
    unsigned long atomActiveWindow_ = 0, atomWmClass_ = 0, atomNetWmName_ = 0,
                  atomUtf8_ = 0, atomWmName_ = 0;

    // DBus screensaver query (spawns a process, so rate-limited)
    std::chrono::steady_clock::time_point lastLockCheck_{};
    bool lockedCached_ = false;
    int lockBackend_ = -1;
    bool haveGdbus_ = true, haveBusctl_ = true;

    // GNOME extension
    std::chrono::steady_clock::time_point lastExtFailure_{};

    // Wayland heuristic state
    std::unordered_map<int, unsigned long long> lastCpu_;
    std::unordered_map<int, bool> guiPidCache_;
    std::vector<int> candidates_;
    std::chrono::steady_clock::time_point lastCandidateScan_{};

    void ensureDisplay();
    void importSessionEnv();
    bool screenOffX11();
    bool screenLockedDbus();
    std::string x11App(std::string &titleOut);
    std::string gnomeExtensionApp();
    std::string heuristicApp();
    std::string readProperty(unsigned long window, unsigned long atom, unsigned long type, bool *okOut);
};
