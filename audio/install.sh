#!/usr/bin/env bash
# Realtek ALC298 Audio Initializer installation script for Samsung Galaxy Book 12
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== [1/4] Compiling ALC298 initializer ==="
make -C "${SCRIPT_DIR}/src" clean 2>/dev/null || true
make -C "${SCRIPT_DIR}/src"

echo "=== [2/4] Installing binary to /usr/local/bin ==="
sudo install -m 755 "${SCRIPT_DIR}/src/alc298_init" /usr/local/bin/alc298_init

echo "=== [3/4] Configuring modprobe audio rules (disabling power_save) ==="
sudo install -m 644 "${SCRIPT_DIR}/configs/audio.conf" /etc/modprobe.d/audio.conf

echo "=== [4/4] Installing and enabling systemd services ==="
sudo install -m 644 "${SCRIPT_DIR}/configs/alc298-init.service" /etc/systemd/system/alc298-init.service
sudo install -m 644 "${SCRIPT_DIR}/configs/alc298-resume.service" /etc/systemd/system/alc298-resume.service

sudo systemctl daemon-reload
sudo systemctl enable alc298-init.service
sudo systemctl enable alc298-resume.service

# Immediate hardware initialization
sudo /usr/local/bin/alc298_init

echo ""
echo "=========================================================="
echo " Realtek ALC298 audio configured and initialized successfully!"
echo " Active services: alc298-init.service & alc298-resume.service"
echo "=========================================================="
