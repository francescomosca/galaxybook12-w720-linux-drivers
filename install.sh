#!/usr/bin/env bash
# Master installation script for Samsung Galaxy Book 12 (SM-W720) Linux Drivers
# Installs all components: OLED Brightness, Audio, Accelerometer, and Cameras.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=========================================================="
echo " Samsung Galaxy Book 12 (SM-W720) - Full Drivers Installer"
echo "=========================================================="
echo ""

# Check sudo permissions upfront
sudo -v

echo ">>> [1/4] Installing Screen (OLED Brightness & Color Profiles)..."
(cd "${SCRIPT_DIR}/screen" && ./install.sh)
echo ""

echo ">>> [2/4] Installing Audio (Realtek ALC298 Initializer)..."
(cd "${SCRIPT_DIR}/audio" && ./install.sh)
echo ""

echo ">>> [3/4] Installing Accelerometer (LIS2HH12 Auto-Rotation)..."
(cd "${SCRIPT_DIR}/accel-sensor" && ./install.sh)
echo ""

echo ">>> [4/4] Installing Cameras (Dual IMX258 / IMX241 & IPU3)..."
(cd "${SCRIPT_DIR}/cameras" && ./install.sh)
echo ""

echo "=========================================================="
echo " ALL GALAXY BOOK 12 DRIVERS INSTALLED SUCCESSFULLY!"
echo "=========================================================="
