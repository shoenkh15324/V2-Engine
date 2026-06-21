#!/bin/bash
set -e

APPS=(
    "v2-demo"
    "v2-cli"
)

for name in "${APPS[@]}"; do
    echo "==> Removing $name..."
    sudo systemctl stop "$name" 2>/dev/null || true
    sudo systemctl disable "$name" 2>/dev/null || true
    sudo rm -f "/etc/systemd/system/${name}.service"
done

sudo systemctl daemon-reload
sudo systemctl reset-failed 2>/dev/null || true

echo "==> Uninstall complete!"
