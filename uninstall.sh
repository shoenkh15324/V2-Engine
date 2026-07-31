#!/bin/bash
set -e

APPS=(
    "v2_main"
    "v2_cli"
    "v2_tui"
)

for name in "${APPS[@]}"; do
    echo "==> Removing $name..."
    sudo systemctl stop "$name" 2>/dev/null || true
    sudo systemctl disable "$name" 2>/dev/null || true
    sudo rm -f "/etc/systemd/system/${name}.service"
    sudo rm -f "/usr/local/bin/$name"
done

sudo rm -f /usr/local/bin/v2

sudo systemctl daemon-reload
sudo systemctl reset-failed 2>/dev/null || true

echo "==> Removing D-Bus policy..."
sudo rm -f /etc/dbus-1/system.d/com.v2.engine.conf
sudo systemctl reload dbus 2>/dev/null || true

echo "==> Uninstall complete!"
