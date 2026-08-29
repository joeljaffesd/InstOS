#!/bin/bash
################################################################################
# MONITOR serial output from an InstOS board via the vendored picocom
#
# Usage:  ./monitor.sh [port]
#   port  serial device to open; auto-detected if omitted
#
# Exit picocom with Ctrl-A Ctrl-X.
################################################################################
set -u
SCRIPT_P=$(readlink -f "$0" || realpath "$0")
SCRIPT_D=$(dirname "$SCRIPT_P")
PICOCOM=$SCRIPT_D/toolchain/picocom/picocom
BAUD=115200

if [ ! -x "$PICOCOM" ] ; then
  echo "ERROR: picocom not found at $PICOCOM (run ./init.sh first)" >&2
  exit 1
fi

if [ $# -gt 0 ] ; then
  PORT=$1
else
  # Prefer a USB-serial adapter (FTDI etc.) over CDC devices (see devloop.sh)
  case "$(uname -s)" in
    Darwin) PORT=$(ls /dev/cu.usbserial-* 2>/dev/null | head -n1)
            [ -n "$PORT" ] || PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -n1) ;;
    Linux)  PORT=$(ls /dev/ttyUSB* 2>/dev/null | head -n1)
            [ -n "$PORT" ] || PORT=$(ls /dev/ttyACM* 2>/dev/null | head -n1) ;;
    *)      echo "ERROR: unsupported platform $(uname -s)" >&2 ; exit 1 ;;
  esac
fi

if [ -z "${PORT:-}" ] || [ ! -e "$PORT" ] ; then
  echo "ERROR: no serial adapter found (pass the port explicitly, e.g. ./monitor.sh /dev/cu.usbserial-0001)" >&2
  exit 1
fi

echo "MONITORING $PORT at $BAUD baud (Ctrl-A Ctrl-X to exit)"
exec "$PICOCOM" -b "$BAUD" --imap lfcrlf "$PORT"
