#!/usr/bin/env bash
# Camera driver uninstallation script for Samsung Galaxy Book 12 (SM-W720)

set -e

KERNEL_VER="$(uname -r)"
MODULE_NAME="galaxybook-cameras"
MODULE_VER="1.0"

echo "=== Removing camera drivers ==="

# Remove from DKMS if registered
if command -v dkms >/dev/null 2>&1; then
    sudo dkms remove "${MODULE_NAME}/${MODULE_VER}" --all 2>/dev/null || true
    sudo rm -rf "/usr/src/${MODULE_NAME}-${MODULE_VER}"
fi

# Remove manual modules
DEST="/usr/lib/modules/${KERNEL_VER}/updates"
sudo rm -f "${DEST}/imx258.ko"
sudo rm -f "${DEST}/imx241.ko"
sudo rm -f "${DEST}/intel_skl_int3472_common.ko"
sudo rm -f "${DEST}/intel_skl_int3472_discrete.ko"
sudo rm -f "${DEST}/ipu-bridge.ko"
sudo rm -f "${DEST}/ipu3-cio2.ko"

# Clean configurations and libcamera tuning files
sudo rm -f /etc/modules-load.d/galaxy-camera.conf
sudo rm -f /etc/modprobe.d/blacklist-watchdog.conf
sudo rm -f /etc/firefox/policies/policies.json
sudo rm -f /usr/share/libcamera/ipa/ipu3/imx258.yaml
sudo rm -f /usr/share/libcamera/ipa/ipu3/imx241.yaml
sudo rm -f /usr/share/libcamera/ipa/ipu3/imx219.yaml

sudo depmod -a

echo " Uninstallation completed."
