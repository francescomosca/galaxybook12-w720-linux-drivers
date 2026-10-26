#!/usr/bin/env bash
# Accelerometer driver uninstallation script for Samsung Galaxy Book 12 (SM-W720)

set -e

KERNEL_VER="$(uname -r)"
MODULE_NAME="galaxybook-sensor"
MODULE_VER="1.0"

echo "=== Removing accelerometer driver and configurations ==="

# Remove from DKMS if registered
if command -v dkms >/dev/null 2>&1; then
    sudo dkms remove "${MODULE_NAME}/${MODULE_VER}" --all 2>/dev/null || true
    sudo rm -rf "/usr/src/${MODULE_NAME}-${MODULE_VER}"
fi

# Remove manual files
sudo rm -f "/usr/lib/modules/${KERNEL_VER}/updates/st_accel_samsung.ko"
sudo rm -f "/lib/modules/${KERNEL_VER}/updates/st_accel_samsung.ko"
sudo rm -f /etc/modules-load.d/galaxybook-sensor.conf
sudo rm -f /etc/udev/hwdb.d/61-sensor-galaxybook12.hwdb

sudo depmod -a
sudo systemd-hwdb update
sudo systemctl restart iio-sensor-proxy 2>/dev/null || true

echo " Uninstallation completed."
