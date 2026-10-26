# Realtek ALC298 Audio Initializer (Samsung Galaxy Book 12)

[🇮🇹 **Leggi in Italiano**](README_IT.md)

High-speed native C initialization utility for the Realtek ALC298 audio codec on Samsung Galaxy Book 12 (SM-W720).

Resolves the hardware issue where internal amplifiers and stereo speakers remain silent after system boot or after resuming from sleep/suspend.

---

## 📁 Directory Structure

- **`src/`**: C source code of the initializer (`alc298_init.c`, `Makefile`).
- **`configs/`**:
  - `alc298-init.service`: Automatic boot-time initialization service.
  - `alc298-resume.service`: Automatic resume hook from suspend/sleep state.
  - `audio.conf`: Modprobe rule (`/etc/modprobe.d/audio.conf`) disabling aggressive HDA power-saving that turns off amplifier pins.
- **`install.sh`**: Automated build and install script.
- **`uninstall.sh`**: Complete uninstallation script.

---

## 🚀 Installation

From the repository root or inside this folder:

```bash
cd audio
./install.sh
```

---

## 🔍 Verification

Check the systemd services status:

```bash
systemctl status alc298-init.service
systemctl status alc298-resume.service
```