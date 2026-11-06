# Ambient Light Sensor & Auto-Brightness (Samsung Galaxy Book 12)

Native Linux kernel driver and high-performance native **C auto-brightness daemon** for the **ROHM BH1733 / BH1730FVC** ambient light sensor (ACPI ID `SAM0204`, I2C address `0x29`) on the Samsung Galaxy Book 12 (SM-W720).

## 🌟 Features
- **Linux IIO Kernel Driver**: Exposes `/sys/bus/iio/devices/iio:device0/in_illuminance_input`.
- **`iio-sensor-proxy` Integration**: Feeds real-time illuminance levels to D-Bus `net.hadess.SensorProxy` (`LightLevel`).
- **Multi-DE & Multi-Distro Native C Daemon**: `galaxybook-autobrightness` (written in native C with GIO/GLib D-Bus) claims the light sensor via `ClaimLight()` and modulates **only** the built-in AMOLED panel (`eDP-1`) across all Linux desktop environments:
  - **KDE Plasma 6**: Per-display `org.kde.ScreenBrightness.Display.SetBrightness` (`IsInternal == true`, silent, external monitors untouched), monitoring `~/.config/kwinoutputconfig.json` (`automaticBrightness`).
  - **KDE Plasma 5**: Legacy `org.kde.Solid.PowerManagement.Actions.BrightnessControl` fallback.
  - **GNOME, Cinnamon, XFCE, MATE, LXQt, Sway, Hyprland, Cosmic, etc.**: Universal `systemd-logind` (`org.freedesktop.login1.Session.SetBrightness` on `/sys/class/backlight/intel_backlight`), respecting GNOME/Cinnamon `ambient-enabled` GSettings and `~/.config/galaxybook-autobrightness.conf`.
- **Smooth Fading & Calibrated Photometric Curve**: Smooth stepping transitions with lower-bound clamping at 30% for $\le 4\text{ lux}$ to avoid aggressive AMOLED panel dimming in dark environments.
- **DKMS Support**: Automatic kernel module recompilation and installation across system kernel updates.

---

## 📁 Directory Structure

- **`src/`**:
  - `bh1730_samsung.c`: Kernel driver source (Linux IIO subsystem with PM suspend/resume support).
  - `galaxybook-autobrightness.c`: High-efficiency native C daemon (GIO D-Bus).
  - `Makefile`: Build instructions for kernel module and user-space binary.
- **`configs/`**:
  - `galaxybook-light-sensor.conf`: Autoload configuration for `/etc/modules-load.d/`.
  - `galaxybook-autobrightness.service`: Systemd user service unit for graphical desktop sessions.
- **`dkms.conf`**: DKMS configuration for `galaxybook-light-sensor/1.0`.
- **`install.sh`**: Automated installation script (DKMS + module loading + user service).
- **`uninstall.sh`**: Complete uninstallation script.

---

## 🚀 Installation

```bash
cd finale/light-sensor
./install.sh
```

## 🗑️ Uninstallation

```bash
cd finale/light-sensor
./uninstall.sh
```
