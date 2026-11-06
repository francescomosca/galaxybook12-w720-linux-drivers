#!/usr/bin/env bash
# Ambient Light Sensor driver uninstallation script for Samsung Galaxy Book 12 (SM-W720)

set -e

KERNEL_VER="$(uname -r)"
MODULE_NAME="galaxybook-light-sensor"
MODULE_VER="1.0"

TARGET_USER="${SUDO_USER:-$USER}"
TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"
TARGET_UID="$(id -u "$TARGET_USER" 2>/dev/null || echo 1000)"

echo "=== [1/4] Stopping and disabling auto-brightness user service ==="
if [ "$EUID" -eq 0 ] && [ "$TARGET_USER" != "root" ]; then
    XDG_RUNTIME_DIR="/run/user/${TARGET_UID}" sudo -u "$TARGET_USER" systemctl --user stop galaxybook-autobrightness.service 2>/dev/null || true
    XDG_RUNTIME_DIR="/run/user/${TARGET_UID}" sudo -u "$TARGET_USER" systemctl --user disable galaxybook-autobrightness.service 2>/dev/null || true
    rm -f "${TARGET_HOME}/.config/systemd/user/galaxybook-autobrightness.service"
    XDG_RUNTIME_DIR="/run/user/${TARGET_UID}" sudo -u "$TARGET_USER" systemctl --user daemon-reload 2>/dev/null || true
else
    systemctl --user stop galaxybook-autobrightness.service 2>/dev/null || true
    systemctl --user disable galaxybook-autobrightness.service 2>/dev/null || true
    rm -f "${HOME}/.config/systemd/user/galaxybook-autobrightness.service"
    systemctl --user daemon-reload 2>/dev/null || true
fi

sudo rm -f /usr/local/bin/galaxybook-autobrightness

echo "=== [2/4] Unloading light sensor module ==="
sudo modprobe -r bh1730_samsung 2>/dev/null || true

echo "=== [3/4] Removing kernel module and DKMS registration ==="
if command -v dkms >/dev/null 2>&1; then
    sudo dkms remove "${MODULE_NAME}/${MODULE_VER}" --all 2>/dev/null || true
    sudo rm -rf "/usr/src/${MODULE_NAME}-${MODULE_VER}"
fi

sudo rm -f "/usr/lib/modules/${KERNEL_VER}/updates/bh1730_samsung.ko"
sudo depmod -a

echo "=== [4/4] Removing boot-time configuration ==="
sudo rm -f /etc/modules-load.d/galaxybook-light-sensor.conf
sudo rm -f /etc/galaxybook-autobrightness.conf
sudo systemctl restart iio-sensor-proxy 2>/dev/null || true

echo "Ambient Light Sensor driver and auto-brightness service uninstalled."
