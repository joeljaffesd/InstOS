#!/bin/bash
set -e

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

# Say Hello
echo -e "${GREEN} Welcome to ${ORANGE}InstOS${NC}"

# Detect Host Platform
case "$(uname -s)-$(uname -m)" in
  Darwin-arm64)  HOST="darwin-arm64" ;;
  Darwin-x86_64)
    echo -e "${RED}Intel macOS is not supported."
    exit 1 ;;
  Linux-x86_64)  HOST="x86_64" ;;
  Linux-aarch64) HOST="aarch64" ;;
  *)
    echo -e "${RED}Unsupported platform: $(uname -s)-$(uname -m)${NC}"
    exit 1 ;;
esac
echo -e "${BLUE}Configuring for host${NC} ${CYAN}${HOST}${NC}."

# Init Submodules
echo -e "${BLUE}Fetching ${PURPLE}submodules${NC}..."
git submodule update --init --recursive
echo -e "${PURPLE}Submodules${NC} ${BLUE}Fetched${NC}."

# Grab ARM cross-compiler toolchain for building
TOOLCHAIN_VERSION="15.2.rel1"
TARGET="aarch64-none-elf"
TOOLCHAIN_DIR="$(pwd)/toolchain"

# Download + extract the toolchain locally (skip if already present)
NAME="arm-gnu-toolchain-${TOOLCHAIN_VERSION}-${HOST}-${TARGET}"
URL="https://developer.arm.com/-/media/Files/downloads/gnu/${TOOLCHAIN_VERSION}/binrel/${NAME}.tar.xz"
if [ ! -x "${TOOLCHAIN_DIR}/${NAME}/bin/${TARGET}-gcc" ]; then
  echo -e "${BLUE}Downloading ${CYAN}ARM GNU Toolchain ${TOOLCHAIN_VERSION}${NC} (${HOST})..."
  mkdir -p "$TOOLCHAIN_DIR"
  curl -L --fail -o "${TOOLCHAIN_DIR}/${NAME}.tar.xz" "$URL"
  echo -e "${BLUE}Extracting ${CYAN}toolchain${NC}..."
  tar -xf "${TOOLCHAIN_DIR}/${NAME}.tar.xz" -C "$TOOLCHAIN_DIR"
  rm "${TOOLCHAIN_DIR}/${NAME}.tar.xz"
  echo -e "${CYAN}Toolchain${NC} ${BLUE}installed${NC}."
else
  echo -e "${CYAN}Toolchain${NC} ${BLUE}already present, skipping download${NC}."
fi
PREFIX="${TOOLCHAIN_DIR}/${NAME}/bin/${TARGET}-"

# Vendor picocom for serial monitoring (built with the host compiler, not the cross toolchain)
PICOCOM_DIR="${TOOLCHAIN_DIR}/picocom"
if [ ! -x "${PICOCOM_DIR}/picocom" ]; then
  echo -e "${BLUE}Vendoring ${CYAN}picocom${NC}..."
  rm -rf "$PICOCOM_DIR"
  git clone --depth 1 --branch 3.1 https://github.com/npat-efault/picocom "$PICOCOM_DIR"
  make -C "$PICOCOM_DIR"
  echo -e "${CYAN}picocom${NC} ${BLUE}installed${NC}."
else
  echo -e "${CYAN}picocom${NC} ${BLUE}already present, skipping${NC}."
fi

# Point Circle's build at the local toolchain
echo -e "${BLUE}Configuring ${YELLOW}Circle${NC}..."
cat > external/circle/Config.mk <<EOF
AARCH = 64
RASPPI = 4
PREFIX64 = ${PREFIX}
# CM4 has no VL805; its USB port hangs off the BCM2711's internal xHCI
# (pairs with otg_mode=1 in config.txt)
DEFINE += -DUSE_XHCI_INTERNAL
EOF
echo -e "${YELLOW}Circle ${BLUE}configured${NC}."

# Build Circle
echo -e "${BLUE}Building ${YELLOW}Circle${NC}..."
cd external/circle; ./makeall clean; ./makeall -j$(getconf _NPROCESSORS_ONLN); cd ../..
echo -e "${YELLOW}Circle ${BLUE}built${NC}."

# We made it!
echo -e "${GREEN}${ORANGE}InstOS ${CYAN}Initialized${NC}."
