#!/bin/bash
################################################################################
# DEV LOOP: build the kernel, push it over serial to the bootloader, monitor
#
# Requires the Circle serial bootloader on the boot media (./flash-bootloader.sh)
# and a 3.3V USB-serial adapter on GPIO14/15.
#
# Each cycle: build, send the reboot magic (a running kernel reboots itself
# into the bootloader), upload via cflashy, then attach picocom as a serial
# monitor. Monitoring lives here, not in a separate terminal — anything else
# holding the port would steal the bootloader handshake and block reflashing.
# No power cycle needed unless the kernel has crashed/hung
# ("waiting for device").
#
# Exit the monitor (Ctrl-A Ctrl-X) to get back to the rebuild prompt, then
# any key to go again (q or Ctrl-C to quit).
#
# FLASHBAUD must match the value the bootloader was built with (default 115200).
# REBOOTMAGIC must match RebootMagic in src/kernel/kernel.cpp.
#
# Usage:  ./devloop.sh [port]
#   port  serial device to use; auto-detected if omitted
################################################################################
set -u
SCRIPT_P=$(readlink -f "$0" || realpath "$0")
SCRIPT_D=$(dirname "$SCRIPT_P")
CFLASHY=$SCRIPT_D/external/circle/tools/cflashy
HEX=build/kernel8-rpi4.hex
FLASHBAUD=${FLASHBAUD:-115200}
USERBAUD=${USERBAUD:-115200}	# kernel's own serial baud (see kernel.cpp)
REBOOTMAGIC=${REBOOTMAGIC:-reboot-instos}

# Color definitions because we like pretty shells scripts that talk to us
RED='\033[0;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
ORANGE='\033[38;5;208m'
PURPLE='\033[0;35m'
BOLD='\033[1m'
NC='\033[0m' # No Color

fatal () {
  echo -e "${RED}ERROR: $*${NC}" >&2
  exit 1
}

# One build + flash + monitor pass
cycle () {
  echo ""
  echo -e "${BLUE}Building ${ORANGE}InstOS${NC}..."
  (cd "$SCRIPT_D" && ./build.sh && make "$HEX") || fatal "build failed"

  echo ""
  echo -e "${BLUE}Flashing ${CYAN}$HEX${NC} ${BLUE}via${NC} ${CYAN}$PORT${NC} ${BLUE}at${NC} ${CYAN}$FLASHBAUD${NC} ${BLUE}baud${NC}"
  echo -e "${YELLOW}(if 'waiting for device' persists, power-cycle the CM4)${NC}"
  # --monitor makes the handshake wait indefinitely for the device (no 15s
  # timeout); its raw RX echo (stderr) is just noise here, so drop it
  "$CFLASHY" "$PORT" "$SCRIPT_D/$HEX" \
    --reboot:"$REBOOTMAGIC" --flashbaud:"$FLASHBAUD" --userbaud:"$USERBAUD" --monitor \
    2>/dev/null || return

  # cflashy exits after the go command; attach picocom to watch the kernel
  echo ""
  "$SCRIPT_D/monitor.sh" "$PORT" || true
}

# Serial port argument
PORT=${1:-}

# Host-side flash tool (plain C, builds with the host compiler)
if [ ! -x "$CFLASHY" ] ; then
  echo -e "${BLUE}Building ${CYAN}cflashy${NC}..."
  make -C "$SCRIPT_D/external/circle/tools" cflashy || fatal "could not build cflashy"
fi

# Serial port: argument, or auto-detect
if [ -z "$PORT" ] ; then
  case "$(uname -s)" in
    Darwin) PORT=$(ls /dev/cu.usbserial-* /dev/cu.usbmodem* 2>/dev/null | head -n1) ;;
    Linux)  PORT=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -n1) ;;
    *)      fatal "unsupported platform $(uname -s)" ;;
  esac
fi
[ -n "${PORT:-}" ] && [ -e "$PORT" ] || fatal "no serial adapter found (pass the port explicitly, e.g. ./devloop.sh /dev/cu.usbserial-0001)"

while true ; do
  cycle
  echo ""
  echo -en "${GREEN}Any key to rebuild+reflash${NC} (${BOLD}q${NC} to quit): "
  read -r -s -n 1 ans || exit 0
  echo ""
  [ "$ans" = "q" ] && exit 0
done
