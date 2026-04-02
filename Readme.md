# System Monitor

A real-time Linux system monitoring application built with C++, Qt6, and CMake. Tracks CPU, memory, disk, network, processes, battery drain, and screen time — similar to Windows Task Manager and iOS Screen Time combined.

## Features

- **Metrics** — Live graphs for CPU (per-core), memory, swap, disk I/O, and network bandwidth with color-coded warnings
- **Processes** — App-grouped process view (like Windows Task Manager), kill/suspend/resume/renice, browser tab identification
- **Battery** — Hardware-accurate power monitoring using Intel RAPL or AMD Energy, per-app power drain attribution, daily/weekly energy tracking
- **Screen Time** — iOS-style screen time tracking with 7-day chart, daily average, idle detection, and per-app usage breakdown
- **Logs** — Configurable alerts (CPU > 90%, memory, disk), timestamped event log, CSV export of system statistics

## Prerequisites

- Linux (tested on Ubuntu 24.04, Linux Mint 22)
- C++17 compiler (GCC 9+ or Clang 10+)
- CMake 3.16+
- Qt6 with Charts and SQL modules
- X11 development libraries
- SQLite3

## Install Dependencies

# System Monitor

Linux system monitor with real-time metrics, process management, battery tracking, and screen time — built with C++ and Qt6.

## Install

```bash
# 1. Install Dependencies
# Ubuntu / Debian / Linux Mint
sudo apt update
sudo apt install cmake g++ qt6-base-dev libqt6charts6-dev libqt6sql6-sqlite \
    libx11-dev libxss-dev libsqlite3-dev

# Fedora
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtcharts-devel \
    libX11-devel libXScrnSaver-devel sqlite-devel

# Arch Linux
sudo pacman -S cmake gcc qt6-base qt6-charts libx11 libxss sqlite

# 2. Clone and build
git clone https://github.com/your-username/system-monitor.git
cd system-monitor
mkdir build && cd build
cmake ..
make -j$(nproc)

# 3. Install system-wide to make it survive reboot
sudo cp SystemMonitor /usr/local/bin/SystemMonitor

# 4. Create desktop shortcut (shows in app menu)
chmod +x ~/.local/share/applications/system-monitor.desktop
cat > ~/.local/share/applications/system-monitor.desktop << 'EOF'
[Desktop Entry]
Name=System Monitor
Exec=/usr/local/bin/SystemMonitor
Type=Application
Categories=System;Monitor;
Comment=System monitor with battery and screen time tracking
EOF

# 5. Set up battery monitoring permissions 
echo 'SUBSYSTEM=="powercap", ACTION=="add", RUN+="/bin/chmod o+r %S%p/energy_uj"' | \
    sudo tee /etc/udev/rules.d/99-rapl.rules
sudo udevadm control --reload-rules
sudo udevadm trigger

# 6. Install background screen time tracker 
cd ..
chmod +x install-tracker.sh
./install-tracker.sh
```

Done. System Monitor is now in your app menu and the screen time tracker starts automatically on every login.

## Verify Everything Works

```bash
# Check System Monitor runs
SystemMonitor

# Check screen time tracker is running
systemctl --user status sysmon-tracker

# Check it starts on boot
systemctl --user is-enabled sysmon-tracker

# Check RAPL permissions
cat /sys/class/powercap/intel-rapl:0/energy_uj
```

## Uninstall

```bash
# Remove System Monitor
sudo rm /usr/local/bin/SystemMonitor
rm ~/.local/share/applications/system-monitor.desktop

# Remove screen time tracker
systemctl --user stop sysmon-tracker
systemctl --user disable sysmon-tracker
rm ~/.local/bin/sysmon-tracker
rm ~/.config/systemd/user/sysmon-tracker.service
systemctl --user daemon-reload

# Remove RAPL rule
sudo rm /etc/udev/rules.d/99-rapl.rules

# Remove data
rm -rf ~/.local/share/SystemMonitor
```


Without RAPL access, battery drain is estimated from CPU usage and battery power draw.

## Project Structure

```
system-monitor/
├── CMakeLists.txt
├── README.md
├── install-tracker.sh              # Installs background tracker
├── sysmon-tracker.service          # systemd service file
├── src/
│   ├── main.cpp
│   ├── MainWindow.h/cpp
│   ├── daemon/
│   │   └── sysmon-tracker.cpp      # Background screen time daemon
│   ├── monitors/
│   │   ├── CpuMonitor.h/cpp        # /proc/stat parsing
│   │   ├── MemoryMonitor.h/cpp     # /proc/meminfo parsing
│   │   ├── DiskMonitor.h/cpp       # /proc/diskstats + statvfs
│   │   ├── NetworkMonitor.h/cpp    # /proc/net/dev parsing
│   │   ├── BatteryMonitor.h/cpp    # /sys/class/power_supply
│   │   ├── BatteryTracker.h/cpp    # Per-app battery drain (SQLite)
│   │   ├── EnergyMonitor.h/cpp     # Intel RAPL / AMD Energy
│   │   └── ScreenTimeTracker.h/cpp # X11 focus tracking + idle detection
│   └── widgets/
│       ├── MetricsTab.h/cpp        # CPU, memory, disk, network charts
│       ├── ProcessesTab.h/cpp      # Process list with app grouping
│       ├── BatteryStatsTab.h/cpp   # Battery and energy UI
│       ├── ScreenTimeTab.h/cpp     # Screen time UI
│       └── LogsTab.h/cpp           # Alerts and logging
└── data/
    └── sysmon.db                   # SQLite database (auto-created at
                                    #   ~/.local/share/SystemMonitor/)
```

## How It Works

| Data | Source | Method |
|------|--------|--------|
| CPU usage | `/proc/stat` | Delta between reads |
| Memory | `/proc/meminfo` | MemTotal - MemAvailable |
| Disk I/O | `/proc/diskstats` | Sector read/write deltas |
| Disk usage | `/proc/mounts` + `statvfs()` | Filesystem stats |
| Network | `/proc/net/dev` | RX/TX byte deltas |
| Processes | `/proc/[pid]/stat`, `statm`, `status` | Per-process parsing |
| Battery | `/sys/class/power_supply/BAT0/` | capacity, power_now, status |
| CPU power | `/sys/class/powercap/intel-rapl:0/` | RAPL energy counters (microjoules) |
| Screen time | X11 `WM_CLASS` + `XScreenSaverQueryInfo` | Focused window + idle detection |
| Browser tabs | `/proc/[pid]/cmdline` + X11 window titles | URL extraction + title matching |
