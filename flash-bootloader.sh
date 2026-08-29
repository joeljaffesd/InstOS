#!/bin/bash
################################################################################
# FLASH the Circle serial bootloader to a CM4 (eMMC via rpiboot) or SD card
#
# The bootloader (David Welch's bootloader07, vendored in Circle) is deployed
# in place of the kernel. On boot it waits on UART0 (GPIO14/15) for a kernel
# image sent by devloop.sh — flash this once, then iterate over serial.
#
# The baud rate is baked in at build time and must match devloop.sh's
# FLASHBAUD (both default to 921600):  FLASHBAUD=115200 ./flash-bootloader.sh
#
# Usage:  ./flash-bootloader.sh [--sd]
#   --sd    deploy to an SD card instead of a CM4 in USB boot mode
################################################################################
set -e
SCRIPT_P=$(readlink -f "$0" || realpath "$0")
SCRIPT_D=$(dirname "$SCRIPT_P")
BOOTLOADER_D=$SCRIPT_D/external/circle/tools/bootloader
FLASHBAUD=${FLASHBAUD:-921600}

# Clean first: the baud rate is a compile-time define, which make can't track
echo "BUILDING bootloader (FLASHBAUD=$FLASHBAUD)..."
make -C "$BOOTLOADER_D" clean
make -C "$BOOTLOADER_D" kernel8-rpi4.img FLASHBAUD="$FLASHBAUD"

# flash.sh handles disk detection, vetting, and file deployment
exec "$SCRIPT_D/flash.sh" "$@" "$BOOTLOADER_D/kernel8-rpi4.img"
