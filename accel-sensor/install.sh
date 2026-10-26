#!/usr/bin/env bash
# Accelerometer driver installation script for Samsung Galaxy Book 12 (SM-W720)
# Supports automated and persistent kernel updates via DKMS when available.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_VER="$(uname -r)"
MODULE_NAME="galaxybook-sensor"
MODULE_VER="1.0"

echo "=== [1/4] Installing accelerometer kernel module (st_accel_samsung) ==="
if command -v dkms >/dev/null 2>&1; then
    echo " -> DKMS detected: configuring accelerometer for automatic kernel rebuilds..."
    
    # Remove previous DKMS registrations if present
    sudo dkms remove "${MODULE_NAME}/${MODULE_VER}" --all 2>/dev/null || true
    sudo rm -rf "/usr/src/${MODULE_NAME}-${MODULE_VER}"
    
    # Copy source code and dkms.conf to /usr/src
    sudo mkdir -p "/usr/src/${MODULE_NAME}-${MODULE_VER}"
    sudo cp -r "${SCRIPT_DIR}/src" "/usr/src/${MODULE_NAME}-${MODULE_VER}/"
    sudo cp "${SCRIPT_DIR}/dkms.conf" "/usr/src/${MODULE_NAME}-${MODULE_VER}/"
    
    # Register, build, and install with DKMS
    sudo dkms add "${MODULE_NAME}/${MODULE_VER}"
    sudo dkms build "${MODULE_NAME}/${MODULE_VER}"
    sudo dkms install "${MODULE_NAME}/${MODULE_VER}"
else
    echo " -> DKMS not found: falling back to manual compilation for kernel ${KERNEL_VER}..."
    DEST="/usr/lib/modules/${KERNEL_VER}/updates"
    make -C "${SCRIPT_DIR}/src" clean
    make -C "${SCRIPT_DIR}/src"
    sudo mkdir -p "${DEST}"
    sudo cp "${SCRIPT_DIR}/src/st_accel_samsung.ko" "${DEST}/"
    sudo depmod -a
fi

echo "=== [2/4] Configuring automatic module loading at boot ==="
sudo mkdir -p /etc/modules-load.d
sudo cp "${SCRIPT_DIR}/configs/galaxybook-sensor.conf" /etc/modules-load.d/galaxybook-sensor.conf

echo "=== [3/4] Configuring rotation calibration (hwdb & iio-sensor-proxy) ==="
sudo mkdir -p /etc/udev/hwdb.d
sudo cp "${SCRIPT_DIR}/configs/61-sensor-galaxybook12.hwdb" /etc/udev/hwdb.d/61-sensor-galaxybook12.hwdb
sudo systemd-hwdb update
sudo udevadm trigger -s iio 2>/dev/null || true
sudo systemctl restart iio-sensor-proxy 2>/dev/null || true

echo "=== [4/4] Loading accelerometer module ==="
sudo modprobe st_accel_samsung 2>/dev/null || true

echo ""
echo "=========================================================="
echo " Accelerometer installation completed successfully!"
echo " The driver will now load automatically on every boot."
if command -v dkms >/dev/null 2>&1; then
    echo " PERSISTENCE: Managed by DKMS (automatic rebuild on kernel updates)."
fi
echo " Rotation test: run 'monitor-sensor'"
echo "=========================================================="
