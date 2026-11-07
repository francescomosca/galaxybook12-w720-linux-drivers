#!/usr/bin/env bash
# Ambient Light Sensor driver & auto-brightness installation script for Samsung Galaxy Book 12 (SM-W720)
# Supports automated and persistent kernel updates via DKMS when available.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_VER="$(uname -r)"
MODULE_NAME="galaxybook-light-sensor"
MODULE_VER="1.0"

# Parse minimum brightness parameter or variable (default: 0 = no lower-bound clamp)
MIN_BRIGHTNESS="${MIN_BRIGHTNESS:-0}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -m|--min-brightness)
            MIN_BRIGHTNESS="$2"
            shift 2
            ;;
        --min-brightness=*)
            MIN_BRIGHTNESS="${1#*=}"
            shift 1
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo "Options:"
            echo "  -m, --min-brightness <0-100>  Set minimum auto-brightness percentage (default: 0)"
            echo "                                Can also be set via MIN_BRIGHTNESS env variable."
            echo "  -h, --help                     Show this help message"
            exit 0
            ;;
        [0-9]*)
            MIN_BRIGHTNESS="$1"
            shift 1
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Validate MIN_BRIGHTNESS
if ! [[ "$MIN_BRIGHTNESS" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "ERROR: Invalid minimum brightness value '${MIN_BRIGHTNESS}'. Must be a number between 0 and 100."
    exit 1
fi
MIN_BRIGHTNESS_INT="${MIN_BRIGHTNESS%%.*}"
if (( MIN_BRIGHTNESS_INT < 0 || MIN_BRIGHTNESS_INT > 100 )); then
    echo "ERROR: Minimum brightness must be between 0 and 100 (got ${MIN_BRIGHTNESS})."
    exit 1
fi

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

echo "=== [5/6] Compiling auto-brightness daemon ==="
make -C "${SCRIPT_DIR}/src/autobrightness"
sudo install -m 755 "${SCRIPT_DIR}/src/autobrightness/galaxybook-autobrightness" /usr/local/bin/galaxybook-autobrightness

# Clean up old root system service if present
sudo systemctl stop galaxybook-autobrightness.service 2>/dev/null || true
sudo systemctl disable galaxybook-autobrightness.service 2>/dev/null || true
sudo rm -f /etc/systemd/system/galaxybook-autobrightness.service

echo "=== [6/6] Configuring auto-brightness (${MIN_BRIGHTNESS}%) and enabling service ==="

# 1. User config in ~/.config/galaxybook-autobrightness.conf
USER_CONFIG_DIR="${TARGET_HOME}/.config"
USER_CONFIG_FILE="${USER_CONFIG_DIR}/galaxybook-autobrightness.conf"

if [ "$EUID" -eq 0 ] && [ "$TARGET_USER" != "root" ]; then
    sudo -u "$TARGET_USER" mkdir -p "${USER_CONFIG_DIR}"
    if [ -f "${USER_CONFIG_FILE}" ]; then
        # Migrate any stale enabled=true override so it doesn't mask DE toggles
        sudo -u "$TARGET_USER" sed -i "s/^enabled=true/# enabled=true/" "${USER_CONFIG_FILE}" 2>/dev/null || true
        if grep -q "^min_brightness=" "${USER_CONFIG_FILE}"; then
            sudo -u "$TARGET_USER" sed -i "s/^min_brightness=.*/min_brightness=${MIN_BRIGHTNESS}/" "${USER_CONFIG_FILE}"
        else
            echo "min_brightness=${MIN_BRIGHTNESS}" | sudo -u "$TARGET_USER" tee -a "${USER_CONFIG_FILE}" >/dev/null
        fi
    else
        cat <<EOF | sudo -u "$TARGET_USER" tee "${USER_CONFIG_FILE}" >/dev/null
# Samsung Galaxy Book 12 Auto-Brightness Configuration
# Minimum brightness percentage limit (0-100)
min_brightness=${MIN_BRIGHTNESS}
# enabled=true (uncomment to force override; defaults to desktop environment toggle)
EOF
    fi
else
    mkdir -p "${USER_CONFIG_DIR}"
    if [ -f "${USER_CONFIG_FILE}" ]; then
        # Migrate any stale enabled=true override so it doesn't mask DE toggles
        sed -i "s/^enabled=true/# enabled=true/" "${USER_CONFIG_FILE}" 2>/dev/null || true
        if grep -q "^min_brightness=" "${USER_CONFIG_FILE}"; then
            sed -i "s/^min_brightness=.*/min_brightness=${MIN_BRIGHTNESS}/" "${USER_CONFIG_FILE}"
        else
            echo "min_brightness=${MIN_BRIGHTNESS}" >> "${USER_CONFIG_FILE}"
        fi
    else
        cat <<EOF > "${USER_CONFIG_FILE}"
# Samsung Galaxy Book 12 Auto-Brightness Configuration
# Minimum brightness percentage limit (0-100)
min_brightness=${MIN_BRIGHTNESS}
# enabled=true (uncomment to force override; defaults to desktop environment toggle)
EOF
    fi
fi

# 2. System-wide default in /etc/galaxybook-autobrightness.conf
sudo mkdir -p /etc
if [ -f /etc/galaxybook-autobrightness.conf ]; then
    sudo sed -i "s/^enabled=true/# enabled=true/" /etc/galaxybook-autobrightness.conf 2>/dev/null || true
    if grep -q "^min_brightness=" /etc/galaxybook-autobrightness.conf; then
        sudo sed -i "s/^min_brightness=.*/min_brightness=${MIN_BRIGHTNESS}/" /etc/galaxybook-autobrightness.conf
    else
        echo "min_brightness=${MIN_BRIGHTNESS}" | sudo tee -a /etc/galaxybook-autobrightness.conf >/dev/null
    fi
else
    cat <<EOF | sudo tee /etc/galaxybook-autobrightness.conf >/dev/null
# Samsung Galaxy Book 12 Auto-Brightness System Defaults
# Minimum brightness percentage limit (0-100)
min_brightness=${MIN_BRIGHTNESS}
# enabled=true (uncomment to force override; defaults to desktop environment toggle)
EOF
fi

# 3. User systemd service installation and activation
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

echo ""
echo "=========================================================="
echo " Light Sensor driver installed successfully!"
echo " The driver will now load automatically on every boot."
if command -v dkms >/dev/null 2>&1; then
    echo " PERSISTENCE: Managed by DKMS (automatic rebuild on kernel updates)."
fi
echo " Service: galaxybook-autobrightness (native C binary, multi-DE, silent)"
echo " Minimum brightness limit: ${MIN_BRIGHTNESS}% (configured in ~/.config/galaxybook-autobrightness.conf)"
echo " Sensor test: run 'monitor-sensor' or check /sys/bus/iio/devices/"
echo "=========================================================="
