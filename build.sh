#!/bin/bash
set -e

make distclean # always clean build for now, for sanity's sake
make -j$(getconf _NPROCESSORS_ONLN)
