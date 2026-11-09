#!/usr/bin/env bash
# Master installation script for Samsung Galaxy Book 12 (SM-W720) Linux Drivers
# Installs all components: OLED Brightness, Audio, Accelerometer, Ambient Light Sensor, and Cameras.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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

echo "=========================================================="
echo " Samsung Galaxy Book 12 (SM-W720) - Full Drivers Installer"
echo "=========================================================="
if [ "$MIN_BRIGHTNESS" != "0" ]; then
    echo " Configuration: Minimum auto-brightness limit set to ${MIN_BRIGHTNESS}%"
else
    echo " Configuration: Minimum auto-brightness limit set to 0% (unrestricted)"
fi
echo ""

# Check sudo permissions upfront
sudo -v

# Check critical build dependencies upfront
MISSING_DEPS=()
if ! command -v dkms >/dev/null 2>&1; then
    MISSING_DEPS+=("dkms")
fi
if [ ! -d "/lib/modules/$(uname -r)/build" ]; then
    MISSING_DEPS+=("linux-headers-$(uname -r)")
fi
if ! command -v pkg-config >/dev/null 2>&1; then
    MISSING_DEPS+=("pkg-config")
elif ! pkg-config --exists gio-2.0 gio-unix-2.0 2>/dev/null; then
    MISSING_DEPS+=("libglib2.0-dev (gio-2.0 / gio-unix-2.0)")
fi

if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    echo "=========================================================="
    echo " ERROR: Missing required build tools or headers:"
    for dep in "${MISSING_DEPS[@]}"; do
        echo "   - ${dep}"
    done
    echo ""
    echo " Please install the required prerequisites before running this installer:"
    echo "   - Ubuntu/Debian: sudo apt install build-essential linux-headers-\$(uname -r) dkms libcamera-tools iio-sensor-proxy libudev-dev pkg-config libglib2.0-dev"
    echo "   - Fedora:        sudo dnf install make gcc kernel-devel kernel-headers dkms libcamera-tools iio-sensor-proxy systemd-devel pkgconf-pkg-config glib2-devel"
    echo "   - Arch Linux:    sudo pacman -S base-devel linux-headers dkms libcamera iio-sensor-proxy pkgconf glib2"
    echo "=========================================================="
    exit 1
fi

echo ">>> [1/5] Installing Screen (OLED Brightness & Color Profiles)..."
(cd "${SCRIPT_DIR}/screen" && ./install.sh)
echo ""

echo ">>> [2/5] Installing Audio (Realtek ALC298 Initializer)..."
(cd "${SCRIPT_DIR}/audio" && ./install.sh)
echo ""

echo ">>> [3/5] Installing Accelerometer (LIS2HH12 Auto-Rotation)..."
(cd "${SCRIPT_DIR}/accel-sensor" && ./install.sh)
echo ""

echo ">>> [4/5] Installing Ambient Light Sensor & Auto-Brightness (BH1733 ALS)..."
(cd "${SCRIPT_DIR}/light-sensor" && MIN_BRIGHTNESS="${MIN_BRIGHTNESS}" ./install.sh --min-brightness "${MIN_BRIGHTNESS}")
echo ""

echo ">>> [5/5] Installing Cameras (Dual IMX258 / IMX241 & IPU3)..."
(cd "${SCRIPT_DIR}/cameras" && ./install.sh)
echo ""

echo "=========================================================="
echo " ALL GALAXY BOOK 12 DRIVERS INSTALLED SUCCESSFULLY!"
echo "=========================================================="
