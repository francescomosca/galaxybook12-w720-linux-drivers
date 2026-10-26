# Accelerometer & Auto-Rotation Driver (Samsung Galaxy Book 12)

Native kernel module and udev calibration for the embedded **STMicroelectronics K2HH / LIS2HH12** accelerometer (ACPI ID `SAM0201`) on Samsung Galaxy Book 12 (SM-W720), providing natural screen orientation for Linux desktops (KDE Plasma Wayland, GNOME, etc.).

---

## 📁 Directory Structure

- **`src/`**: Kernel driver C source code (`st_accel_samsung.c`, `st_accel.h`, `Makefile`).
- **`configs/`**:
  - `61-sensor-galaxybook12.hwdb`: Mount matrix calibration (`ACCEL_MOUNT_MATRIX=0, -1, 0; -1, 0, 0; 0, 0, -1`) mapping the sensor to the AMOLED panel orientation.
  - `galaxybook-sensor.conf`: Automatic module loading configuration for `/etc/modules-load.d/`.
- **`install.sh`**: Automated build, deployment, and hwdb registration script.
- **`uninstall.sh`**: Complete removal script.

---

## 🚀 Installation

From the repository root or inside this folder:

```bash
cd accel-sensor
./install.sh
```

---

## 🔍 Testing & Verification

To verify accelerometer readings and orientation in real-time:

```bash
monitor-sensor
```

When tilting the tablet, orientation will smoothly report:
- `normal` (standard landscape)
- `bottom-up` (inverted landscape)
- `right-up` / `left-up` (portrait orientations)
