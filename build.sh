#!/bin/bash
set -e

# Usage:  ./build.sh [examples/<name>]
# With no argument, the Makefile's EXAMPLE default decides what gets built.
make distclean # always clean build for now, for sanity's sake
if [ $# -gt 0 ]; then
  [ -d "$1" ] || { echo "ERROR: no such example: $1" >&2; exit 1; }
  make EXAMPLE="$1" -j$(getconf _NPROCESSORS_ONLN)
else
  make -j$(getconf _NPROCESSORS_ONLN)
fi
