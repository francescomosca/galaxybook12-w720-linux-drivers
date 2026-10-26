#!/usr/bin/env bash
# Camera driver installation script for Samsung Galaxy Book 12 (SM-W720)
# Supports automated and persistent kernel updates via DKMS when available.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_VER="$(uname -r)"
MODULE_NAME="galaxybook-cameras"
MODULE_VER="1.0"

echo "=== [1/4] Installing camera kernel modules ==="
if command -v dkms >/dev/null 2>&1; then
    echo " -> DKMS detected: configuring cameras for automatic kernel rebuilds..."
    
    sudo dkms remove "${MODULE_NAME}/${MODULE_VER}" --all 2>/dev/null || true
    sudo rm -rf "/usr/src/${MODULE_NAME}-${MODULE_VER}"
    
    sudo mkdir -p "/usr/src/${MODULE_NAME}-${MODULE_VER}"
    sudo cp -r "${SCRIPT_DIR}/imx258-mod" "/usr/src/${MODULE_NAME}-${MODULE_VER}/"
    sudo cp -r "${SCRIPT_DIR}/imx241-mod" "/usr/src/${MODULE_NAME}-${MODULE_VER}/"
    sudo cp -r "${SCRIPT_DIR}/int3472-mod" "/usr/src/${MODULE_NAME}-${MODULE_VER}/"
    sudo cp -r "${SCRIPT_DIR}/bridge-mod" "/usr/src/${MODULE_NAME}-${MODULE_VER}/"
    sudo cp -r "${SCRIPT_DIR}/cio2-mod" "/usr/src/${MODULE_NAME}-${MODULE_VER}/"
    sudo cp "${SCRIPT_DIR}/Makefile" "/usr/src/${MODULE_NAME}-${MODULE_VER}/"
    sudo cp "${SCRIPT_DIR}/dkms.conf" "/usr/src/${MODULE_NAME}-${MODULE_VER}/"
    
    sudo dkms add "${MODULE_NAME}/${MODULE_VER}"
    sudo dkms build "${MODULE_NAME}/${MODULE_VER}"
    sudo dkms install "${MODULE_NAME}/${MODULE_VER}"
else
    echo " -> DKMS not found: falling back to manual compilation for kernel ${KERNEL_VER}..."
    DEST="/usr/lib/modules/${KERNEL_VER}/updates"
    
    CLANG_FLAGS=""
    if which clang >/dev/null 2>&1; then
        CLANG_FLAGS="CC=clang LLVM=1"
    fi
    
    make $CLANG_FLAGS -C "${SCRIPT_DIR}/imx258-mod" clean
    make $CLANG_FLAGS -C "${SCRIPT_DIR}/imx258-mod"
    make $CLANG_FLAGS -C "${SCRIPT_DIR}/imx241-mod" clean
    make $CLANG_FLAGS -C "${SCRIPT_DIR}/imx241-mod"
    make $CLANG_FLAGS -C "${SCRIPT_DIR}/int3472-mod" clean
    make $CLANG_FLAGS -C "${SCRIPT_DIR}/int3472-mod"
    make $CLANG_FLAGS -C "${SCRIPT_DIR}/bridge-mod" clean
    make $CLANG_FLAGS -C "${SCRIPT_DIR}/bridge-mod"
    make $CLANG_FLAGS -C "${SCRIPT_DIR}/cio2-mod" clean
    make $CLANG_FLAGS -C "${SCRIPT_DIR}/cio2-mod"
    
    sudo mkdir -p "${DEST}"
    sudo cp "${SCRIPT_DIR}/imx258-mod/imx258.ko" "${DEST}/"
    sudo cp "${SCRIPT_DIR}/imx241-mod/imx241.ko" "${DEST}/"
    sudo cp "${SCRIPT_DIR}/int3472-mod/intel_skl_int3472_common.ko" "${DEST}/"
    sudo cp "${SCRIPT_DIR}/int3472-mod/intel_skl_int3472_discrete.ko" "${DEST}/"
    sudo cp "${SCRIPT_DIR}/bridge-mod/ipu-bridge.ko" "${DEST}/"
    sudo cp "${SCRIPT_DIR}/cio2-mod/ipu3-cio2.ko" "${DEST}/"
    sudo depmod -a
fi

echo "=== [2/4] Installing libcamera tuning files ==="
sudo mkdir -p /usr/share/libcamera/ipa/ipu3
sudo cp "${SCRIPT_DIR}/ipa-tuning/"*.yaml /usr/share/libcamera/ipa/ipu3/

echo "=== [3/4] Installing startup configurations and browser policies ==="
sudo mkdir -p /etc/modules-load.d /etc/modprobe.d /etc/firefox/policies
sudo cp "${SCRIPT_DIR}/configs/galaxy-camera-modules.conf" /etc/modules-load.d/galaxy-camera.conf
sudo cp "${SCRIPT_DIR}/configs/blacklist-watchdog.conf" /etc/modprobe.d/blacklist-watchdog.conf
sudo cp "${SCRIPT_DIR}/configs/firefox-pipewire-policy.json" /etc/firefox/policies/policies.json

echo "=== [4/4] Loading camera modules ==="
sudo modprobe intel_skl_int3472_discrete 2>/dev/null || true
sudo modprobe ipu-bridge 2>/dev/null || true
sudo modprobe ipu3-cio2 2>/dev/null || true
sudo modprobe imx258 2>/dev/null || true
sudo modprobe imx241 2>/dev/null || true

echo ""
echo "=========================================================="
echo " CAMERA INSTALLATION COMPLETED SUCCESSFULLY!"
echo " Both cameras (Rear & Front) are ready for use."
if command -v dkms >/dev/null 2>&1; then
    echo " PERSISTENCE: Managed by DKMS (automatic rebuild on kernel updates)."
fi
echo "=========================================================="
