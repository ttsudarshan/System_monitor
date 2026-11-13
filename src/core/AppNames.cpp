#include "AppNames.h"

#include <cctype>
#include <unordered_map>

namespace {

std::string lower(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    return s.substr(b);
}

bool has(const std::string &s, const char *t) { return s.find(t) != std::string::npos; }
bool starts(const std::string &s, const char *t) { return s.rfind(t, 0) == 0; }

} // namespace

std::string AppNames::canonical(const std::string &raw) {
    std::string n = trim(lower(raw));
    if (n.empty()) return n;

    // Kernel threads (comm looks like "kworker/u32:1", "irq/97-rtw89_pci", "jbd2/nvme0n1p2-8")
    static const char *const kernelPrefixes[] = {
        "kworker", "irq/", "jbd2/", "rcu_", "ksoftirqd", "migration/", "kcompactd", "kswapd",
        "kthreadd", "cpuhp/", "idle_inject", "watchdog", "oom_reaper", "khugepaged", "kblockd",
        "ext4-", "xfs", "nvme-", "scsi_", "usb-", "card", "kdevtmpfs", "netns", "writeback", nullptr};
    for (int i = 0; kernelPrefixes[i]; ++i)
        if (starts(n, kernelPrefixes[i])) return "kernel";

    // Browsers. Order matters: "chromium" and "msedge" both contain "chrome"-ish fragments.
    if (has(n, "brave")) return "brave";
    if (has(n, "librewolf")) return "librewolf";
    if (has(n, "firefox") || has(n, "isolated web co") || n == "web content"
        || has(n, "privileged cont") || n == "webextensions" || n == "rdd process" || n == "socket process")
        return "firefox";
    if (has(n, "chromium")) return "chromium";
    if (has(n, "msedge") || has(n, "microsoft-edge")) return "microsoft-edge";
    if (has(n, "chrome")) return "google-chrome";
    if (has(n, "vivaldi")) return "vivaldi";
    if (n == "opera" || starts(n, "opera")) return "opera";

    // Editors / IDEs
    if (n == "code" || starts(n, "code ") || starts(n, "code-") || n == "code - oss" || has(n, "codium")
        || has(n, "vscode"))
        return "vscode";

    // Terminals (comm names are truncated to 15 chars, hence the prefixes)
    static const char *const terminals[] = {
        "gnome-terminal", "xterm", "konsole", "tilix", "alacritty", "kitty", "wezterm",
        "terminator", "xfce4-terminal", "mate-terminal", "deepin-terminal", "urxvt",
        "rxvt", "foot", "ptyxis", "ghostty", "st-256color", "lxterminal", "qterminal",
        "sakura", "guake", "yakuake", "tilda", "x-terminal-emul", nullptr};
    for (int i = 0; terminals[i]; ++i)
        if (has(n, terminals[i])) return "terminal";

    // Communication / media
    if (has(n, "slack")) return "slack";
    if (has(n, "discord")) return "discord";
    if (has(n, "spotify")) return "spotify";
    if (has(n, "telegram")) return "telegram";
    if (has(n, "thunderbird")) return "thunderbird";
    if (has(n, "zoom")) return "zoom";
    if (has(n, "teams")) return "teams";
    if (has(n, "vlc")) return "vlc";
    if (n == "obs" || starts(n, "obs-") || has(n, "obsproject")) return "obs-studio";

    // Office / graphics / games
    if (has(n, "libreoffice") || starts(n, "soffice")) return "libreoffice";
    if (has(n, "gimp")) return "gimp";
    if (has(n, "steam")) return "steam";

    // The desktop itself (nemo-desktop, nautilus desktop_window, plasma desktop)
    if (has(n, "-desktop") || n == "desktop_window" || n == "desktop") return "desktop";

    // File managers
    if (has(n, "nemo")) return "nemo";
    if (has(n, "nautilus")) return "nautilus";
    if (has(n, "dolphin")) return "dolphin";
    if (has(n, "thunar")) return "thunar";

    // Desktop shells and system monitors
    if (has(n, "gnome-system")) return "gnome-system-monitor";
    if (has(n, "systemmonitor") || n == "system monitor") return "system monitor";
    if (has(n, "gnome-shell")) return "gnome-shell";
    if (has(n, "cinnamon") && !has(n, "setting") && !has(n, "screensaver")) return "cinnamon";
    if (has(n, "gnome-screensho")) return "gnome-screenshot";  // comm is truncated to 15 chars

    // System components, grouped for the energy view
    if (starts(n, "xorg") || n == "x" || starts(n, "/usr/lib/xorg")) return "xorg";
    if (has(n, "xwayland")) return "xwayland";
    if (has(n, "pipewire") || has(n, "wireplumber")) return "pipewire";
    if (has(n, "pulseaudio")) return "pulseaudio";
    if (has(n, "networkmanager")) return "networkmanager";
    if (starts(n, "systemd")) return "systemd";

    return n;
}

std::string AppNames::display(const std::string &canonical) {
    static const std::unordered_map<std::string, std::string> names = {
        {"brave", "Brave Browser"},
        {"firefox", "Firefox"},
        {"librewolf", "LibreWolf"},
        {"chromium", "Chromium"},
        {"google-chrome", "Google Chrome"},
        {"microsoft-edge", "Microsoft Edge"},
        {"vivaldi", "Vivaldi"},
        {"opera", "Opera"},
        {"vscode", "VS Code"},
        {"terminal", "Terminal"},
        {"slack", "Slack"},
        {"discord", "Discord"},
        {"spotify", "Spotify"},
        {"telegram", "Telegram"},
        {"thunderbird", "Thunderbird"},
        {"zoom", "Zoom"},
        {"teams", "Microsoft Teams"},
        {"vlc", "VLC"},
        {"obs-studio", "OBS Studio"},
        {"libreoffice", "LibreOffice"},
        {"gimp", "GIMP"},
        {"steam", "Steam"},
        {"desktop", "Desktop"},
        {"nemo", "Files (Nemo)"},
        {"nautilus", "Files (Nautilus)"},
        {"dolphin", "Files (Dolphin)"},
        {"thunar", "Files (Thunar)"},
        {"gnome-system-monitor", "GNOME System Monitor"},
        {"system monitor", "System Monitor"},
        {"gnome-shell", "GNOME Shell"},
        {"cinnamon", "Cinnamon Desktop"},
        {"gnome-screenshot", "Screenshot"},
        {"xorg", "Display Server (Xorg)"},
        {"xwayland", "XWayland"},
        {"pipewire", "Audio (PipeWire)"},
        {"pulseaudio", "Audio (PulseAudio)"},
        {"networkmanager", "Network Manager"},
        {"systemd", "systemd"},
        {"kernel", "Kernel"},
    };
    auto it = names.find(canonical);
    if (it != names.end()) return it->second;
    if (canonical.empty()) return canonical;
    std::string s = canonical;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

bool AppNames::isBrowser(const std::string &c) {
    return c == "brave" || c == "firefox" || c == "librewolf" || c == "chromium"
        || c == "google-chrome" || c == "microsoft-edge" || c == "vivaldi" || c == "opera";
}

bool AppNames::isDesktopNoise(const std::string &comm) {
    std::string n = lower(comm);
    static const char *const prefixes[] = {
        "csd-", "gsd-", "xdg-", "at-spi", "ibus", "gvfs", "dbus", "pipewire", "wireplumber",
        "pulseaudio", "systemd", "dconf", "evolution-", "goa-", "tracker", "blueman",
        "nm-applet", "mintreport", "mintupdate", "xapp-", "polkit", "gnome-keyring",
        "cinnamon-sessio", "cinnamon-launch", "cinnamon-screen", "cinnamon-killer",
        "mutter", "gnome-shell", "kwin", "plasmashell", "sway", "hyprland", "xwayland",
        "xorg", "sysmon-tracker", "systemmonitor", "gjs", "gnome-software", "packagekit",
        "update-notifier", "snapd", "fwupd", "colord", "upowerd", "cupsd", "sshd",
        "gdm", "lightdm", "sddm", "xfce4-panel", "xfce4-notifyd", "mate-panel",
        "lxqt-panel", "waybar", "swaybg", "kded", "kactivity", "baloo", "nemo-desktop",
        "cinnamon", nullptr};
    for (int i = 0; prefixes[i]; ++i)
        if (starts(n, prefixes[i])) return true;
    static const char *const fragments[] = {
        "-tray", "-applet", "-daemon", "-agent", "-portal", "-indicator", "notify",
        "notification", "screensaver", "-locker", "-settings-d", nullptr};
    for (int i = 0; fragments[i]; ++i)
        if (has(n, fragments[i])) return true;
    return false;
}
