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

```bash
# Ubuntu / Debian / Linux Mint
sudo apt update
sudo apt install cmake g++ qt6-base-dev libqt6charts6-dev libqt6sql6-sqlite \
    libx11-dev libxss-dev libsqlite3-dev

# Fedora
sudo dnf install cmake gcc-c++ qt6-qtbase-devel qt6-qtcharts-devel \
    libX11-devel libXScrnSaver-devel sqlite-devel

# Arch Linux
sudo pacman -S cmake gcc qt6-base qt6-charts libx11 libxss sqlite
```

## Build & Run

```bash
git clone https://github.com/your-username/system-monitor.git
cd system-monitor
mkdir build && cd build
cmake ..
make -j$(nproc)
./SystemMonitor
```

## Install Background Screen Time Tracker (Optional)

The screen time tracker runs as a background service so it records app usage even when System Monitor is closed. It uses idle detection to stop counting when you're away.

```bash
# From the project root directory
chmod +x install-tracker.sh
./install-tracker.sh
```

This will:
1. Build the lightweight `sysmon-tracker` daemon
2. Install it to `~/.local/bin/`
3. Set up a systemd user service that starts on login
4. Start tracking immediately

### Manage the Background Tracker

```bash
# Check if running
systemctl --user status sysmon-tracker

# View live logs
journalctl --user -u sysmon-tracker -f

# Stop tracking
systemctl --user stop sysmon-tracker

# Restart
systemctl --user restart sysmon-tracker

# Disable from starting on login
systemctl --user disable sysmon-tracker

# Uninstall completely
systemctl --user stop sysmon-tracker
systemctl --user disable sysmon-tracker
rm ~/.local/bin/sysmon-tracker
rm ~/.config/systemd/user/sysmon-tracker.service
systemctl --user daemon-reload
```

## Battery Monitoring Note

For accurate hardware power readings (Intel RAPL / AMD Energy), you may need to run with elevated permissions:

```bash
# Option 1: Run as root (not recommended for daily use)
sudo ./SystemMonitor

# Option 2: Grant read access to RAPL (recommended)
sudo chmod o+r /sys/class/powercap/intel-rapl:0/energy_uj
sudo chmod o+r /sys/class/powercap/intel-rapl:0/*/energy_uj

# Option 3: Make it permanent with a udev rule
echo 'SUBSYSTEM=="powercap", ACTION=="add", RUN+="/bin/chmod o+r %S%p/energy_uj"' | \
    sudo tee /etc/udev/rules.d/99-rapl.rules
sudo udevadm control --reload-rules
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
