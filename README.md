# Samsung Galaxy Book 12 (SM-W720) - Linux Hardware Fixes & Drivers

[🇮🇹 **Leggi la documentazione in Italiano**](README_IT.md)

Modular collection of kernel drivers, patches, and user-space utilities to enable full hardware support for the **Samsung Galaxy Book 12 (SM-W720)** 2-in-1 tablet PC on Linux (Arch Linux, CachyOS, Ubuntu, Debian, Fedora, etc. with Kernel 6.x and 7.x).

> [!WARNING]
> **EXPERIMENTAL**: These drivers and configurations were developed on CachyOS and have not been tested on other devices and/or different Linux distributions. Use with caution and at your own risk.

---

## 💻 Hardware Compatibility Overview

| Component | Status | Driver Type | Supported Distros | Notes |
| :--- | :---: | :--- | :--- | :--- |
| **Cameras (Dual)** | ✅ | Kernel Modules (`imx258`, `imx241`, `ipu-bridge`, `cio2`, `int3472`) | All (Kernel 6.x / 7.x) | Rear (13MP Sony IMX258) + Front (5MP Sony IMX241) via IPU3 & PipeWire |
| **OLED Brightness** | ✅ | User-space daemon (DP-AUX direct bus) | All (KDE, GNOME, XFCE) | True hardware backlight control without screen flickering |
| **AMOLED Color Profiles** | ✅ | User-space script + GUI (`kdialog` / `zenity`) | All | Adobe RGB, DCI-P3, sRGB, Adaptive profiles |
| **Audio (ALC298)** | ✅ | User-space C init + systemd sleep hook | All | Resolves silent internal speakers at boot and after sleep/resume |
| **Accelerometer** | ✅ | Kernel module (`st_accel_samsung`) + `hwdb` | All (systemd-based) | Auto-rotation 1:1 match with OLED orientation (Wayland / X11) |
| **Ambient Light Sensor** | ❌ | Missing driver / Work in progress | — | Automatic ambient backlight adjustment is currently unsupported |

---

## 📦 Prerequisites & System Dependencies

Before compiling and installing, make sure your system has the standard build tools, DKMS, and kernel headers installed:

### Arch Linux / CachyOS / Manjaro
```bash
sudo pacman -S base-devel linux-headers dkms libcamera iio-sensor-proxy
```

### Ubuntu / Debian / Linux Mint
```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r) dkms libcamera-tools iio-sensor-proxy zenity
```

### Fedora
```bash
sudo dnf install make gcc kernel-devel kernel-headers dkms libcamera-tools iio-sensor-proxy zenity
```

---

## 🚀 Installation

The easiest way to get everything working is using the root **master installer**. It configures and installs all hardware drivers in the correct order, sets up DKMS for kernel persistence, and starts background services:

```bash
git clone https://github.com/francescomosca/samsung-galaxybook12-linux-drivers.git
cd samsung-galaxybook12-linux-drivers
./install.sh
```

The master script will automatically install and configure:
1. **Screen**: Compiles the DP-AUX brightness daemon, installs sudoers rules, and enables the systemd service.
2. **Audio**: Compiles the ALC298 codec initializer and enables boot/resume services.
3. **Accelerometer**: Builds and registers `st_accel_samsung` into DKMS, sets up `hwdb` rotation calibration.
4. **Cameras**: Builds and registers the 5 camera modules into DKMS, installs libcamera IPU3 tuning profiles and Firefox policies.

---

## 📂 Modular Installation (Individual Components)

If you only need specific fixes, each directory can also be installed or uninstalled independently:

### 1. 📷 [cameras/](cameras/)
Enables both front and rear cameras via the Intel IPU3 image signal processor.
- Patch for `ipu-bridge`, CSI-2 receiver `ipu3-cio2`, and PMIC/GPIO `int3472`.
- Custom drivers for rear **Sony IMX258** and front **Sony IMX241**.
- Libcamera tuning profiles (`.yaml`) and Firefox PipeWire policies.
```bash
cd cameras
./install.sh
```

### 2. 💡 [screen/](screen/)
Controls the Samsung AMOLED display brightness via DisplayPort AUX channel (`/dev/drm_dp_aux*`) and provides profile switching.
- High-efficiency C background daemon (`galaxybook-brightness`).
- Color profile switcher GUI (`galaxybook-colorprofile`) with KDE and GNOME/GTK support.
- Systemd service for persistence.
```bash
cd screen
./install.sh
```

### 3. 🔊 [audio/](audio/)
Direct hardware initialization for the Realtek ALC298 audio codec.
- Native C initializer (`alc298_init`) that wakes the audio amplifier at boot and after resume.
- Disables aggressive HDA power-saving that mutes sound.
```bash
cd audio
./install.sh
```

### 4. 🧭 [accel-sensor/](accel-sensor/)
Native kernel driver for the STMicroelectronics LIS2HH12 accelerometer (ACPI ID `SAM0201`).
- Mount matrix calibration (`hwdb`) calibrated for 1:1 natural rotation.
- Seamless integration with `iio-sensor-proxy` and desktop environments (KDE Plasma Wayland, GNOME).
```bash
cd accel-sensor
./install.sh
```

---

## 🔄 Automated Kernel Updates (DKMS)
The kernel modules for the cameras and accelerometer integrate natively with **DKMS (Dynamic Kernel Module Support)**.
Whenever your distribution upgrades the Linux kernel (via `pacman -Syu`, `apt upgrade`, or `dnf upgrade`), DKMS will **automatically recompile and install** the drivers in the background for the newly installed kernel. No manual reinstallation is required.

---

## 🌟 Credits & Acknowledgments
- Based on the initial hardware research from the [SamsungGalaxyBook12 repository by Teetoow](https://github.com/Teetoow/SamsungGalaxyBook12).
- Linux kernel camera subsystems maintained by the Linux Media community (Dan Scally, Hans de Goede, Sakari Ailus).
- Assisted reverse engineering, driver adaptation, and fixes developed with AI pair programming.

## 📄 License
This repository is licensed under the [GNU General Public License v2.0 (GPL-2.0)](LICENSE).
