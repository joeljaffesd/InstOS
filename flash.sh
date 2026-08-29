#!/bin/bash
################################################################################
# DEPLOY a Circle kernel image to a CM4 (eMMC via rpiboot) or SD card
#
# Circle kernel*.img files are bare kernel binaries, not disk images: they must
# be copied to a FAT32 boot partition alongside the Raspberry Pi firmware from
# boot/ and a config.txt — never dd'ed to the raw device.
#
# Usage:  ./flash.sh [--sd] [examples/<name> | path/to/kernelN.img]
#   --sd             deploy to an SD card (insert/remove detection) instead of
#                    a CM4 in USB boot mode (nRPIBOOT jumper + rpiboot)
#   examples/<name>  build that example, then deploy it
#   path/to/*.img    deploy an existing image as-is; nothing is built
#   (no argument)    build the Makefile's default example, then deploy it
################################################################################
set -u
SCRIPT_P=$(readlink -f "$0" || realpath "$0")
SCRIPT_D=$(dirname "$SCRIPT_P")
BOOT_D=$SCRIPT_D/external/circle/boot
CONFIG_D=$SCRIPT_D/config		# boot-partition payload: config-extra.txt, cmdline.txt
IMAGE=$SCRIPT_D/build/kernel8-rpi4.img
MODE=cm4

OS=$(uname -s)

################################################################################
# HELPERS (formerly sourced from common.sh)
################################################################################
winge () {
  echo "ERROR: $*" >&2
}

fatal () {
  winge "$*"
  exit 1
}

try () {
  echo "+ $*"
  "$@" || fatal "COMMAND failed: $*"
}

next () {
  echo ""
  echo ">>> $*"
}

anykey () {
  read -r -s -n 1 -p "Press any key to continue..."
  echo ""
}

exclaim () {
  echo ""
  echo "*** $* ***"
}

dbg () {
  echo "[diag] $*" >&2
}

################################################################################
# DISK detection (macOS)
################################################################################
darwin_check_cm4 () {
  if ! command -v rpiboot > /dev/null ; then
    fatal "rpiboot not installed (brew install rpiboot)"
  fi
  if ! system_profiler SPUSBDataType 2>/dev/null | grep -q "BCM2711 Boot" ; then
    fatal "NO CM4 in boot mode found on USB (expected 'BCM2711 Boot'). Check the nRPIBOOT jumper and power."
  fi
  echo "CM4 detected in USB boot mode"
}

rpiboot_launch () {
  echo "BOOTING CM4 to USB MSD mode..."
  try sudo rpiboot
  sleep 2
}

darwin_list_disks () {
  diskutil list | grep -oE '^/dev/disk[0-9]+' | sort
}

# WAIT for a disk absent from $1 to enumerate; sets DISK
darwin_wait_new_disk () {
  local disks_before="$1"
  local what="$2"
  local disks_after="" new_disks
  dbg "disks before: $(echo $disks_before)"
  echo -n "WAITING for $what to appear as a disk"
  DISK=""
  for i in $(seq 1 30) ; do
    disks_after=$(darwin_list_disks)
    new_disks=$(comm -13 <(echo "$disks_before") <(echo "$disks_after"))
    if [ -n "$new_disks" ] ; then
      echo
      dbg "poll $i: new disks: $(echo $new_disks)"
      DISK=$(echo "$new_disks" | head -n1)
      break
    fi
    echo -n "."
    sleep 1
  done
  echo
  if [ -z "$DISK" ] ; then
    winge "NO $what appeared as a disk"
    dbg "full diskutil list follows:"
    diskutil list >&2
    exit 1
  fi
  dbg "selected DISK=$DISK"
}

darwin_find_emmc () {
  local disks_before
  disks_before=$(darwin_list_disks)
  rpiboot_launch
  darwin_wait_new_disk "$disks_before" "eMMC"
}

darwin_find_sdcard () {
  local disks_before
  next "REMOVE the SD card (if inserted)"
  anykey
  disks_before=$(darwin_list_disks)
  next "INSERT the SD card"
  anykey
  darwin_wait_new_disk "$disks_before" "SD card"
}

# VET the target disk and confirm with the user
darwin_vet_dev () {
  local info size ans
  info=$(diskutil info "$DISK") || fatal "NO such disk $DISK"
  # SANITY: never the Mac's own storage — external disk, or removable media
  # (the built-in SDXC reader reports "Device Location: Internal")
  if ! echo "$info" | grep -Eq "Device Location: *External|Removable Media: *Removable" ; then
    winge "$DISK is not an external disk or removable media — refusing to touch it"
    echo "$info" >&2
    exit 1
  fi
  if ! echo "$info" | grep -q "Virtual: *No" ; then
    winge "$DISK is a virtual disk — refusing to touch it"
    echo "$info" >&2
    exit 1
  fi

  size=$(echo "$info" | grep "Disk Size" | sed 's/.*Disk Size: *//;s/ (.*//')
  echo "DEPLOY target: $DISK ($size)"
  echo ""
  diskutil list "$DISK"
  echo ""
}

# MOUNT the first FAT partition of $DISK, formatting the disk if none exists;
# sets MNT
darwin_mount_fat () {
  local part=${DISK}s1 ans
  if ! diskutil info "$part" 2>/dev/null | grep -Eq "Type \(Bundle\): *msdos|File System Personality: *(MS-DOS|FAT32|ExFAT)" ; then
    echo "NO FAT partition found on $DISK."
    read -r -p "About to ERASE $DISK and create one FAT32 partition. Type 'yes' to continue: " ans
    [ "$ans" = "yes" ] || fatal "ABORTED by user"
    try diskutil eraseDisk MS-DOS CIRCLE MBRFormat "$DISK"
  else
    read -r -p "About to write Circle boot files to $part. Type 'yes' to continue: " ans
    [ "$ans" = "yes" ] || fatal "ABORTED by user"
  fi
  diskutil mount "$part" > /dev/null 2>&1 || true
  MNT=$(diskutil info "$part" | grep "Mount Point" | sed 's/.*Mount Point: *//')
  [ -n "$MNT" ] && [ -d "$MNT" ] || fatal "COULD not mount $part"
  echo "MOUNTED $part at $MNT"
}

################################################################################
# DISK detection (Linux)
################################################################################
linux_get_dev () {
  local dev rm ans
  lsblk -dno NAME,SIZE,RM,MODEL || true
  read -r -p "DEPLOY device (e.g. /dev/sdc): " dev
  [ -b "$dev" ] || fatal "NO such block device $dev"
  rm=$(lsblk -dno RM "$dev" | tr -d ' ')
  if [ "$rm" != "1" ] ; then
    winge "$dev is not removable — refusing to touch it (edit script to override)"
    exit 1
  fi
  read -r -p "About to write Circle boot files to ${dev}1. Type 'yes' to continue: " ans
  [ "$ans" = "yes" ] || fatal "ABORTED by user"
  DISK=$dev
}

linux_mount_fat () {
  local part=${DISK}1
  [ -b "$part" ] || fatal "NO partition $part — partition the device with one FAT32 partition first"
  MNT=$(mktemp -d)
  try sudo mount "$part" "$MNT"
  LINUX_MOUNTED=1
  echo "MOUNTED $part at $MNT"
}

################################################################################
# FIRMWARE + file deployment
################################################################################
ensure_firmware () {
  if [ ! -e "$BOOT_D/start4.elf" ] ; then
    echo "Raspberry Pi firmware not present in boot/ — downloading (requires wget)..."
    command -v wget > /dev/null || fatal "wget not installed (brew install wget)"
    try make -C "$BOOT_D" firmware
  fi
}

deploy_files () {
  local base cfg f
  base=$(basename "$IMAGE")

  # CONFIG: kernel8*/kernel_2712 are AArch64 targets, everything else AArch32
  case "$base" in
    kernel8*|kernel_2712*) cfg=config64.txt ;;
    *)                     cfg=config32.txt ;;
  esac

  echo ""
  echo "COPYING boot files to $MNT..."
  for f in "$BOOT_D"/*.bin "$BOOT_D"/*.elf "$BOOT_D"/*.dat "$BOOT_D"/*.dtb ; do
    [ -e "$f" ] || continue
    try cp "$f" "$MNT/"
  done
  for f in "$BOOT_D"/*.dtbo ; do
    [ -e "$f" ] || continue
    mkdir -p "$MNT/overlays"
    try cp "$f" "$MNT/overlays/"
  done
  try cp "$BOOT_D/$cfg" "$MNT/config.txt"
  # InstOS-specific config.txt additions (e.g. forced HDMI mode for the 9.3" panel)
  if [ -e "$CONFIG_D/config-extra.txt" ] ; then
    try sh -c "cat '$CONFIG_D/config-extra.txt' >> '$MNT/config.txt'"
  fi
  # KERNEL options, parsed by Circle's CKernelOptions (e.g. loglevel=)
  [ -e "$CONFIG_D/cmdline.txt" ] && try cp "$CONFIG_D/cmdline.txt" "$MNT/"
  echo ""
  echo "COPYING kernel $base..."
  try cp "$IMAGE" "$MNT/"
  sync
}

################################################################################
# MAIN
################################################################################
if [ "$OS" != "Linux" ] && [ "$OS" != "Darwin" ] ; then
  fatal "Supported on Linux and macOS only"
fi

EXAMPLE=
IMAGE_GIVEN=0

while [ $# -gt 0 ] ; do
  case "$1" in
    --sd) MODE=sd ;;
    -h|--help) sed -n '2,/^####/p' "$SCRIPT_P" ; exit 0 ;;
    # An explicit image is deployed as-is; anything else names an example
    # directory to build (same argument build.sh takes).
    *.img) IMAGE=$1 ; IMAGE_GIVEN=1 ;;
    *) [ -d "$1" ] || fatal "no such example directory: $1 (or pass a kernel*.img)"
       EXAMPLE=$1 ;;
  esac
  shift
done

[ -n "$EXAMPLE" ] && [ "$IMAGE_GIVEN" = "1" ] \
  && fatal "give an example directory OR an image, not both"

# Build only once the arguments are known: -h must not rebuild anything, and
# an explicit image is deployed as handed to us rather than rebuilt over.
if [ "$IMAGE_GIVEN" = "0" ] ; then
  if [ -n "$EXAMPLE" ] ; then
    ./build.sh "$EXAMPLE"
  else
    ./build.sh
  fi
fi

[ -e "$IMAGE" ] || fatal "KERNEL image $IMAGE MISSING (build it first, e.g. make in the sample dir)"
case "$(basename "$IMAGE")" in
  kernel*.img) ;;
  *) fatal "$IMAGE does not look like a Circle kernel image (kernel*.img)" ;;
esac

ensure_firmware

LINUX_MOUNTED=0
if [ "$OS" = "Darwin" ] ; then
  if [ "$MODE" = "cm4" ] ; then
    darwin_check_cm4
    darwin_find_emmc
  else
    darwin_find_sdcard
  fi
  darwin_vet_dev
  darwin_mount_fat
else
  if [ "$MODE" = "cm4" ] ; then
    command -v rpiboot > /dev/null || fatal "rpiboot not installed"
    rpiboot_launch
  fi
  linux_get_dev
  linux_mount_fat
fi

deploy_files

if [ "$OS" = "Darwin" ] ; then
  try diskutil eject "$DISK"
elif [ "$LINUX_MOUNTED" = "1" ] ; then
  try sudo umount "$MNT"
  rmdir "$MNT"
fi

if [ "$MODE" = "cm4" ] ; then
  echo "Power off the unit, remove the nRPIBOOT jumper, and power on to boot."
fi

exclaim SUCCESS
