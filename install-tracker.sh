#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== Installing sysmon-tracker daemon ==="

# Build via CMake (daemon target is part of the main build)
echo "[1/4] Building..."
cd "$SCRIPT_DIR"
cmake -B build -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build build --target sysmon-tracker -j"$(nproc)"

# Install binary
echo "[2/4] Installing binary to ~/.local/bin/..."
mkdir -p ~/.local/bin
install -m 755 build/sysmon-tracker ~/.local/bin/sysmon-tracker

# Install systemd service
echo "[3/4] Installing systemd user service..."
mkdir -p ~/.config/systemd/user
cp "$SCRIPT_DIR/sysmon-tracker.service" ~/.config/systemd/user/sysmon-tracker.service

# Enable and (re)start, so a daemon that is already running picks up the new binary
echo "[4/4] Enabling and starting service..."
systemctl --user daemon-reload
systemctl --user enable sysmon-tracker.service
systemctl --user restart sysmon-tracker.service
sleep 1

echo ""
echo "=== Done! ==="
echo "Status: $(systemctl --user is-active sysmon-tracker.service)"
journalctl --user -u sysmon-tracker --no-pager -n 3 -o cat 2>/dev/null || true
echo ""
echo "Useful commands:"
echo "  systemctl --user status sysmon-tracker     # Check status"
echo "  journalctl --user -u sysmon-tracker -f     # View live logs"
echo "  systemctl --user restart sysmon-tracker    # Restart after rebuild"
echo "  systemctl --user stop sysmon-tracker       # Stop"
