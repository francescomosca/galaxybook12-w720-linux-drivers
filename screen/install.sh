#!/usr/bin/env bash
# OLED brightness daemon & AMOLED color profile switcher installer
# Samsung Galaxy Book 12 (SM-W720)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== [1/5] Compiling brightness driver ==="
make -C "${SCRIPT_DIR}/src" clean 2>/dev/null || true
make -C "${SCRIPT_DIR}/src"

echo "=== [2/5] Installing binaries to /usr/local/bin ==="
sudo install -m 755 "${SCRIPT_DIR}/src/brightness" /usr/local/bin/galaxybook-brightness
sudo install -m 755 "${SCRIPT_DIR}/galaxybook-brightness-wrapper.sh" /usr/local/bin/galaxybook-brightness-wrapper.sh
sudo install -m 755 "${SCRIPT_DIR}/galaxybook-colorprofile" /usr/local/bin/galaxybook-colorprofile

echo "=== [3/5] Setting up configuration and sudoers rules ==="
if [ ! -f /etc/galaxybook-colorprofile.conf ]; then
    echo "3" | sudo tee /etc/galaxybook-colorprofile.conf > /dev/null
fi
sudo chmod 666 /etc/galaxybook-colorprofile.conf
sudo install -m 440 "${SCRIPT_DIR}/configs/galaxybook-brightness-sudoers" /etc/sudoers.d/galaxybook-brightness

echo "=== [4/5] Installing application menu shortcut ==="
sudo install -m 644 "${SCRIPT_DIR}/configs/galaxybook-colorprofile.desktop" /usr/share/applications/galaxybook-colorprofile.desktop

echo "=== [5/5] Installing and starting systemd service ==="
sudo install -m 644 "${SCRIPT_DIR}/configs/galaxybook-brightness.service" /etc/systemd/system/galaxybook-brightness.service
sudo systemctl daemon-reload
sudo systemctl enable galaxybook-brightness.service
sudo systemctl restart galaxybook-brightness.service

echo ""
echo "=========================================================="
echo " OLED brightness driver & color profile switcher installed!"
echo " Active service: galaxybook-brightness.service"
echo "=========================================================="
