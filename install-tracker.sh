#!/bin/bash
set -e

echo "=== Installing sysmon-tracker daemon ==="

# Build the daemon
echo "[1/4] Building daemon..."
cd "$(dirname "$0")"
mkdir -p build-daemon
cd build-daemon
g++ -std=c++17 -O2 -o sysmon-tracker ../src/daemon/sysmon-tracker.cpp \
    $(pkg-config --cflags dbus-1) \
    -lX11 -lXss -lsqlite3 -lpthread $(pkg-config --libs dbus-1)
cd ..

# Install binary
echo "[2/4] Installing binary..."
mkdir -p ~/.local/bin
cp build-daemon/sysmon-tracker ~/.local/bin/sysmon-tracker
chmod +x ~/.local/bin/sysmon-tracker

# Install systemd service
echo "[3/4] Installing systemd service..."
mkdir -p ~/.config/systemd/user
cp sysmon-tracker.service ~/.config/systemd/user/sysmon-tracker.service

# Enable and start
echo "[4/4] Enabling and starting service..."
systemctl --user daemon-reload
systemctl --user enable sysmon-tracker.service
systemctl --user start sysmon-tracker.service

echo ""
echo "=== Done! ==="
echo "Status: $(systemctl --user is-active sysmon-tracker.service)"
echo ""
echo "Useful commands:"
echo "  systemctl --user status sysmon-tracker    # Check status"
echo "  journalctl --user -u sysmon-tracker -f    # View logs"
echo "  systemctl --user stop sysmon-tracker      # Stop"
echo "  systemctl --user restart sysmon-tracker   # Restart"