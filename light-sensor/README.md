# Ambient Light Sensor & Auto-Brightness (Samsung Galaxy Book 12)

Native Linux kernel driver and high-performance native **C auto-brightness daemon** for the **ROHM BH1733 / BH1730FVC** ambient light sensor (ACPI ID `SAM0204`, I2C address `0x29`) on the Samsung Galaxy Book 12 (SM-W720).

## 🌟 Features
- **Linux IIO Kernel Driver**: Exposes `/sys/bus/iio/devices/iio:device0/in_illuminance_input`.
- **`iio-sensor-proxy` Integration**: Feeds real-time illuminance levels to D-Bus `net.hadess.SensorProxy` (`LightLevel`).
- **Native C Daemon**: `galaxybook-autobrightness` (written in native C with GIO/GLib D-Bus) claims the light sensor via `ClaimLight()` and modulates AMOLED panel brightness via `setBrightnessSilent` on KDE Solid PowerManagement (completely silent, no OSD popups, non-blocking asynchronous calls).
- **Smooth Fading & Calibrated Photometric Curve**: Smooth stepping transitions with lower-bound clamping at 30% for $\le 4\text{ lux}$ to avoid aggressive AMOLED panel dimming in dark environments.
- **DKMS Support**: Automatic kernel module recompilation and installation across system kernel updates.

> [!NOTE]
> The kernel module and `iio-sensor-proxy` are desktop-agnostic and work across all Linux desktop environments (GNOME, Sway, Hyprland, etc.).
> The user-space auto-brightness daemon (`galaxybook-autobrightness`) currently targets **KDE Plasma (PowerDevil)**. The installer automatically detects non-KDE environments, installs the kernel module, and safely skips user-space daemon activation with a warning.

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
