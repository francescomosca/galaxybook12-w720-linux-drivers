#!/usr/bin/env bash
set -e

echo "=== Uninstalling ALC298 audio initializer ==="
sudo systemctl stop alc298-init.service 2>/dev/null || true
sudo systemctl stop alc298-resume.service 2>/dev/null || true
sudo systemctl disable alc298-init.service 2>/dev/null || true
sudo systemctl disable alc298-resume.service 2>/dev/null || true
sudo rm -f /etc/systemd/system/alc298-init.service
sudo rm -f /etc/systemd/system/alc298-resume.service
sudo rm -f /usr/local/bin/alc298_init
sudo rm -f /etc/modprobe.d/audio.conf
sudo systemctl daemon-reload
echo "Uninstallation completed."
