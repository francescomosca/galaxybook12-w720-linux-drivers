#!/usr/bin/env bash
# Ambient Light Sensor driver & auto-brightness installation script for Samsung Galaxy Book 12 (SM-W720)
# Supports automated and persistent kernel updates via DKMS when available.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_VER="$(uname -r)"
MODULE_NAME="galaxybook-light-sensor"
MODULE_VER="1.0"

# Determine target non-root user and home directory
TARGET_USER="${SUDO_USER:-$USER}"
TARGET_HOME="$(getent passwd "$TARGET_USER" | cut -d: -f6)"
TARGET_UID="$(id -u "$TARGET_USER" 2>/dev/null || echo 1000)"

echo "=== [1/6] Installing light sensor kernel module (bh1730_samsung) ==="
# Clean build artifacts first
make -C "${SCRIPT_DIR}/src" clean >/dev/null 2>&1 || true

if command -v dkms >/dev/null 2>&1; then
    echo " -> DKMS detected: configuring light sensor for automatic kernel rebuilds..."
    
    # Remove previous DKMS registrations if present
    sudo dkms remove "${MODULE_NAME}/${MODULE_VER}" --all 2>/dev/null || true
    sudo rm -rf "/usr/src/${MODULE_NAME}-${MODULE_VER}"
    
    # Copy clean source code and dkms.conf to /usr/src
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
    make -C "${SCRIPT_DIR}/src" modules
    sudo mkdir -p "${DEST}"
    sudo cp "${SCRIPT_DIR}/src/bh1730_samsung.ko" "${DEST}/"
    sudo depmod -a
fi

echo "=== [2/6] Configuring automatic module loading at boot ==="
sudo mkdir -p /etc/modules-load.d
sudo cp "${SCRIPT_DIR}/configs/galaxybook-light-sensor.conf" /etc/modules-load.d/galaxybook-light-sensor.conf

echo "=== [3/6] Loading light sensor module ==="
sudo modprobe -r bh1730_samsung 2>/dev/null || true
sudo modprobe bh1730_samsung 2>/dev/null || true

echo "=== [4/6] Restarting sensor proxy ==="
sudo systemctl restart iio-sensor-proxy 2>/dev/null || true

echo "=== [5/6] Checking desktop environment and compiling auto-brightness daemon ==="
IS_KDE=false
if [[ "${XDG_CURRENT_DESKTOP:-}" =~ (KDE|plasma|Plasma) ]] || \
   [[ "${DESKTOP_SESSION:-}" =~ (KDE|plasma|Plasma) ]] || \
   pgrep -u "$TARGET_USER" -x plasmashell >/dev/null 2>&1 || \
   pgrep -u "$TARGET_USER" -x kded6 >/dev/null 2>&1 || \
   pgrep -u "$TARGET_USER" -x kded5 >/dev/null 2>&1; then
    IS_KDE=true
fi

make -C "${SCRIPT_DIR}/src" daemon
sudo install -m 755 "${SCRIPT_DIR}/src/galaxybook-autobrightness" /usr/local/bin/galaxybook-autobrightness

# Clean up old root system service if present
sudo systemctl stop galaxybook-autobrightness.service 2>/dev/null || true
sudo systemctl disable galaxybook-autobrightness.service 2>/dev/null || true
sudo rm -f /etc/systemd/system/galaxybook-autobrightness.service

if [ "$IS_KDE" = true ]; then
    echo "=== [6/6] Enabling and starting user auto-brightness service for KDE Plasma ==="
    USER_SYSTEMD_DIR="${TARGET_HOME}/.config/systemd/user"
    if [ "$EUID" -eq 0 ] && [ "$TARGET_USER" != "root" ]; then
        sudo -u "$TARGET_USER" mkdir -p "${USER_SYSTEMD_DIR}"
        sudo -u "$TARGET_USER" cp "${SCRIPT_DIR}/configs/galaxybook-autobrightness.service" "${USER_SYSTEMD_DIR}/galaxybook-autobrightness.service"
        XDG_RUNTIME_DIR="/run/user/${TARGET_UID}" sudo -u "$TARGET_USER" systemctl --user daemon-reload 2>/dev/null || true
        XDG_RUNTIME_DIR="/run/user/${TARGET_UID}" sudo -u "$TARGET_USER" systemctl --user enable galaxybook-autobrightness.service 2>/dev/null || true
        XDG_RUNTIME_DIR="/run/user/${TARGET_UID}" sudo -u "$TARGET_USER" systemctl --user restart galaxybook-autobrightness.service 2>/dev/null || true
    else
        mkdir -p "${USER_SYSTEMD_DIR}"
        cp "${SCRIPT_DIR}/configs/galaxybook-autobrightness.service" "${USER_SYSTEMD_DIR}/galaxybook-autobrightness.service"
        systemctl --user daemon-reload
        systemctl --user enable galaxybook-autobrightness.service
        systemctl --user restart galaxybook-autobrightness.service
    fi
else
    echo "=== [6/6] Non-KDE environment detected ==="
    echo "------------------------------------------------------------------"
    echo " WARNING: Non-KDE environment detected (Desktop: '${XDG_CURRENT_DESKTOP:-unknown}')."
    echo " WARNING: The auto-brightness daemon currently targets KDE Plasma (PowerDevil)."
    echo " WARNING: The IIO light sensor kernel driver and iio-sensor-proxy integration"
    echo "          are active and provide standard Linux IIO ambient light data."
    echo " WARNING: Skipping activation of the KDE auto-brightness user service."
    echo "------------------------------------------------------------------"
fi

echo ""
echo "=========================================================="
echo " Light Sensor driver installed successfully!"
echo " The driver will now load automatically on every boot."
if command -v dkms >/dev/null 2>&1; then
    echo " PERSISTENCE: Managed by DKMS (automatic rebuild on kernel updates)."
fi
if [ "$IS_KDE" = true ]; then
    echo " Service: galaxybook-autobrightness (native C binary, silent, no OSD)"
fi
echo " Sensor test: run 'monitor-sensor' or check /sys/bus/iio/devices/"
echo "=========================================================="
