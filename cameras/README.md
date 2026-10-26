# Dual Camera Drivers for Samsung Galaxy Book 12 (SM-W720)

Complete package containing patched kernel modules, calibration profiles, and configurations to enable **both cameras (Rear Sony IMX258 and Front Sony IMX241)** on Samsung Galaxy Book 12 running modern Linux kernels (6.x / 7.x).

---

## 📁 Directory Structure

- **`imx258-mod/`**: Patched kernel driver for rear camera (Sony IMX258, 26MHz clock, custom link frequencies).
- **`imx241-mod/`**: Patched kernel driver for front camera (Sony IMX241, direct GPIO power sequencing on pins 72, 75, 85, 143, 120 and libcamera helper).
- **`int3472-mod/`**: Intel INT3472 discrete PMIC/GPIO driver with hardware pin mappings for Galaxy Book 12.
- **`bridge-mod/`**: `ipu-bridge` driver with ACPI SSDB link frequencies registered for both sensor IDs.
- **`cio2-mod/`**: CSI-2 receiver `ipu3-cio2` driver with media controller link fixes.
- **`ipa-tuning/`**: Intel IPU3 libcamera ISP tuning calibration files (`imx258.yaml`, `imx241.yaml`, `imx219.yaml`).
- **`configs/`**:
  - `galaxy-camera-modules.conf`: Automatic boot-time module loading in `/etc/modules-load.d/`.
  - `blacklist-watchdog.conf`: Disables Intel iTCO watchdogs in `/etc/modprobe.d/` preventing reboot stalls.
  - `firefox-pipewire-policy.json`: System-wide policy enabling PipeWire camera support in Firefox.
- **`install.sh`**: Automated compilation, deployment, and configuration script.

---

## 🚀 Installation

From the repository root or inside this folder:

```bash
cd cameras
./install.sh
```

The installer automatically detects whether your kernel uses Clang/LLVM (e.g. CachyOS) or standard GCC, compiles all 5 kernel modules, places them in `/usr/lib/modules/$(uname -r)/updates/`, updates module dependencies via `depmod`, installs the tuning files to `/usr/share/libcamera/ipa/ipu3/`, and enables autoloading.

---

## 🔍 Testing Cameras

After installation (or reboot):

1. **Check detected media devices**:
   ```bash
   media-ctl -p -d /dev/media0
   ```
2. **List cameras with libcamera**:
   ```bash
   cam -l
   ```
3. **Capture a test image or stream**:
   ```bash
   qcam
   ```
   Or open any web browser (Firefox / Chromium) with WebRTC and PipeWire.
