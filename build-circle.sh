#!/bin/bash
################################################################################
# REBUILD the Circle libraries against config/circle.mk
#
# Needed after any edit to config/circle.mk (its settings are compiled into
# the libs) or after touching Circle source in the fork. App builds pick the
# fresh libs up automatically; rebuild your example afterwards as usual.
#
# A full clean rebuild by default, because compile-time DEFINEs are invisible
# to make's dependency tracking.
#
# Usage:  ./build-circle.sh [--quick]
#   --quick   skip the clean (fine for Circle source edits; NOT for
#             config/circle.mk changes)
################################################################################
set -e
SCRIPT_P=$(readlink -f "$0" || realpath "$0")
SCRIPT_D=$(dirname "$SCRIPT_P")
CIRCLE_D=$SCRIPT_D/external/circle

if [ ! -f "$CIRCLE_D/Config.mk" ]; then
  echo "ERROR: $CIRCLE_D/Config.mk not found (run ./init.sh first)" >&2
  exit 1
fi

cd "$CIRCLE_D"
if [ "${1:-}" != "--quick" ]; then
  ./makeall clean
fi
./makeall -j"$(getconf _NPROCESSORS_ONLN)"
echo "Circle libraries rebuilt."
