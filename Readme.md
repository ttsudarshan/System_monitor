# System Monitor

A real-time Linux system monitoring application built with C++ and Qt6.

## Install & Run

```bash
# Install dependencies (Ubuntu/Debian/Mint)
sudo apt update
sudo apt install cmake g++ qt6-base-dev libqt6charts6-dev

# Clone and build
git clone https://github.com/your-username/system-monitor.git
cd system-monitor
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run
./SystemMonitor
```