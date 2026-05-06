# System Monitor

A real-time Linux system monitoring application built with C++, Qt6, and CMake. Tracks CPU, memory, disk, network, processes, battery drain, and screen time — similar to Windows Task Manager and iOS Screen Time combined.

## Features

- **Metrics** — Live graphs for CPU (per-core), memory, swap, disk I/O, and network bandwidth with color-coded warnings
- **Processes** — App-grouped process view (like Windows Task Manager), kill/suspend/resume/renice
- **Battery** — Hardware-accurate power monitoring using Intel RAPL or AMD Energy, per-app power drain attribution, daily/weekly energy tracking
- **Screen Time** — iOS-style screen time tracking with 7-day bar chart, daily average, idle/lock detection, per-app usage breakdown, and **per-site tab breakdown for browsers** (Brave, Firefox, Chrome, etc.)
- **Logs** — Configurable alerts (CPU > 90%, memory, disk), timestamped event log, CSV export

## How Screen Time Works

Screen time is collected by a **standalone background daemon** (`sysmon-tracker`) that runs as a systemd user service, completely independent of the GUI. Closing the GUI window does not stop tracking.

- Tracks the focused application every second via X11 `_NET_ACTIVE_WINDOW` / `WM_CLASS`
- For browsers, reads `_NET_WM_NAME` (window title) and extracts the site name (e.g. "YouTube", "GitHub") to give a per-tab breakdown nested under each browser entry
- Pauses counting when the screen is locked or blanked — counts time like a phone (screen on = tracking, screen off = paused)
- Writes to SQLite at `~/.local/share/SystemMonitor/sysmon.db`; the GUI reads from the same file

## Distro & Desktop Compatibility

The build itself runs on any modern Linux (Ubuntu, Mint, Fedora, RHEL/CentOS Stream 9+, Arch). All the system-stat metrics — CPU, memory, disk, network, battery, RAPL — read from `/proc` and `/sys` and behave the same everywhere.

**Screen time tracking is where your display server matters.** The daemon uses three detection paths in this priority order:

| Session | Detection path | Accuracy |
|---|---|---|
| **Any Xorg / X11 session** (incl. "GNOME on Xorg", XFCE, Cinnamon, MATE, KDE/X11) | `_NET_ACTIVE_WINDOW` + `WM_CLASS` via Xlib | **Full** — focused app + browser tab name |
| GNOME on Wayland *with the companion Shell extension installed* | DBus call to `org.sysmon.Tracker` | Full app name (no tab titles) |
| KDE/Sway/Hyprland/etc. on Wayland, **or** GNOME Wayland without the extension | `/proc/*/maps` CPU-delta heuristic | **Approximate** — picks busiest GUI process, misses idle-but-focused apps, no tab names |

**Recommendation:** if you're on Fedora Workstation, Fedora KDE Spin, or CentOS Stream and want the most accurate screen time data, log into an **Xorg session** at the login screen ("GNOME on Xorg" / "Plasma (X11)"). Wayland-native sessions on non-GNOME desktops will still record data, just less precisely. Lock/blank detection also relies on GNOME's IdleMonitor DBus interface, so on KDE/Sway under Wayland a locked screen may keep counting time.

## Prerequisites

- Linux (tested on Ubuntu 24.04, Linux Mint 22; should build on Fedora 39+ and RHEL/CentOS Stream 9+)
- C++17 compiler (GCC 9+ or Clang 10+)
- CMake 3.16+
- Qt6 with Widgets, Charts, and SQL modules
- X11 / XScreenSaver development libraries
- SQLite3 development library

## Install

### 1. Install dependencies

**Debian / Ubuntu / Mint:**

```bash
sudo apt update
sudo apt install cmake g++ \
    qt6-base-dev libqt6charts6-dev libqt6sql6-sqlite \
    libx11-dev libxss-dev libsqlite3-dev
```

**Fedora:**

```bash
sudo dnf install cmake gcc-c++ \
    qt6-qtbase-devel qt6-qtcharts-devel \
    libX11-devel libXScrnSaver-devel sqlite-devel
```

**RHEL / CentOS Stream 9+** (enable CRB/EPEL first if Qt6 isn't found):

```bash
sudo dnf install epel-release
sudo dnf config-manager --set-enabled crb
sudo dnf install cmake gcc-c++ \
    qt6-qtbase-devel qt6-qtcharts-devel \
    libX11-devel libXScrnSaver-devel sqlite-devel
```

### 2. Clone and build

Both the GUI and the daemon are built together with a single CMake command:

```bash
git clone https://github.com/ttsudarshan/system-monitor.git
cd system-monitor
cmake -B build
cmake --build build -j$(nproc)
```

### 3. Install the GUI system-wide

```bash
sudo cp build/SystemMonitor /usr/local/bin/SystemMonitor
```

Add a desktop shortcut so it appears in your app menu:

```bash
mkdir -p ~/.local/share/applications
cat > ~/.local/share/applications/system-monitor.desktop << 'EOF'
[Desktop Entry]
Name=System Monitor
Exec=/usr/local/bin/SystemMonitor
Type=Application
Categories=System;Monitor;
Comment=System monitor with battery and screen time tracking
EOF
```

### 4. Install the screen time daemon

The daemon must be installed and running for screen time to accumulate when the GUI is closed.

**Option A — automated (recommended):**

```bash
chmod +x install-tracker.sh
./install-tracker.sh
```

**Option B — manual:**

```bash
# Install the binary
cmake --install build --prefix ~/.local   # puts it at ~/.local/bin/sysmon-tracker

# Install and enable the systemd service
mkdir -p ~/.config/systemd/user
cp sysmon-tracker.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now sysmon-tracker
```

### 5. Set up battery monitoring permissions (optional)

Required for hardware-accurate per-app power drain via Intel RAPL:

```bash
echo 'SUBSYSTEM=="powercap", ACTION=="add", RUN+="/bin/chmod o+r %S%p/energy_uj"' | \
    sudo tee /etc/udev/rules.d/99-rapl.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Without this, battery drain is estimated from CPU usage and the system power draw reported by `/sys/class/power_supply`.

## Verify Everything Works

```bash
# Launch the GUI
SystemMonitor

# Check the screen time daemon is running
systemctl --user status sysmon-tracker

# Confirm it starts automatically on login
systemctl --user is-enabled sysmon-tracker

# Check RAPL permissions (optional)
cat /sys/class/powercap/intel-rapl:0/energy_uj
```

## Uninstall

```bash
# Remove the GUI
sudo rm /usr/local/bin/SystemMonitor
rm ~/.local/share/applications/system-monitor.desktop

# Remove the screen time daemon
systemctl --user stop sysmon-tracker
systemctl --user disable sysmon-tracker
rm ~/.local/bin/sysmon-tracker
rm ~/.config/systemd/user/sysmon-tracker.service
systemctl --user daemon-reload

# Remove the RAPL udev rule (if installed)
sudo rm /etc/udev/rules.d/99-rapl.rules

# Remove all tracked data
rm -rf ~/.local/share/SystemMonitor
```

## Project Structure

```
system-monitor/
├── CMakeLists.txt                  # Builds both GUI and daemon
├── Readme.md
├── install-tracker.sh              # One-step daemon install helper
├── sysmon-tracker.service          # systemd user service unit
└── src/
    ├── main.cpp
    ├── MainWindow.h/cpp
    ├── daemon/
    │   ├── sysmon-tracker.cpp      # Daemon entry point + main loop
    │   ├── sysmon-tracker-core.cpp # X11 focus detection, DB writes, tab extraction
    │   └── sysmon-tracker-core.h
    ├── monitors/
    │   ├── CpuMonitor.h/cpp        # /proc/stat parsing
    │   ├── MemoryMonitor.h/cpp     # /proc/meminfo parsing
    │   ├── DiskMonitor.h/cpp       # /proc/diskstats + statvfs
    │   ├── NetworkMonitor.h/cpp    # /proc/net/dev parsing
    │   ├── BatteryMonitor.h/cpp    # /sys/class/power_supply
    │   ├── BatteryTracker.h/cpp    # Per-app battery drain (SQLite)
    │   ├── EnergyMonitor.h/cpp     # Intel RAPL / AMD Energy
    │   └── ScreenTimeTracker.h/cpp # GUI-side DB queries + fallback tracking
    └── widgets/
        ├── MetricsTab.h/cpp        # CPU, memory, disk, network charts
        ├── ProcessesTab.h/cpp      # Process list with app grouping
        ├── BatteryStatsTab.h/cpp   # Battery and energy UI
        ├── ScreenTimeTab.h/cpp     # Screen time UI (nested browser tabs)
        └── LogsTab.h/cpp           # Alerts and log export
```

## How It Works

| Data | Source | Method |
|------|--------|--------|
| CPU usage | `/proc/stat` | Delta between reads |
| Memory | `/proc/meminfo` | MemTotal − MemAvailable |
| Disk I/O | `/proc/diskstats` | Sector read/write deltas |
| Disk usage | `/proc/mounts` + `statvfs()` | Filesystem stats |
| Network | `/proc/net/dev` | RX/TX byte deltas |
| Processes | `/proc/[pid]/stat`, `statm`, `status` | Per-process parsing |
| Battery | `/sys/class/power_supply/BAT0/` | capacity, power_now, status |
| CPU power | `/sys/class/powercap/intel-rapl:0/` | RAPL energy counters (µJ) |
| Screen time | X11 `_NET_ACTIVE_WINDOW` + `WM_CLASS` | Focused window per second |
| Browser tabs | X11 `_NET_WM_NAME` (window title) | Site name extracted from title |
| Screen lock | `XScreenSaverQueryInfo` / GNOME IdleMonitor | Pause tracking when blanked |
