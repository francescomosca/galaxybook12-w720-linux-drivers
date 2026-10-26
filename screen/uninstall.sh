#!/usr/bin/env bash
set -e

echo "=== Uninstalling OLED brightness driver and profiles ==="
sudo systemctl stop galaxybook-brightness.service 2>/dev/null || true
sudo systemctl disable galaxybook-brightness.service 2>/dev/null || true
sudo rm -f /etc/systemd/system/galaxybook-brightness.service
sudo rm -f /usr/local/bin/galaxybook-brightness
sudo rm -f /usr/local/bin/galaxybook-brightness-wrapper.sh
sudo rm -f /usr/local/bin/galaxybook-colorprofile
sudo rm -f /etc/sudoers.d/galaxybook-brightness
sudo rm -f /usr/share/applications/galaxybook-colorprofile.desktop
sudo rm -f /etc/galaxybook-colorprofile.conf
sudo systemctl daemon-reload
echo "Uninstallation completed."
