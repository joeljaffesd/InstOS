#!/bin/bash

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

# Init Submodules
echo -e "${BLUE}Fetching ${PURPLE}submodules${NC}..."
git submodule update --init --recursive
echo -e "${PURPLE}Submodules${NC} ${BLUE}Fetched${NC}."

# Build Circle
echo -e "${BLUE}Building ${YELLOW}Circle${NC}..."
cd external/circle; ./makeall clean; ./makeall -j$(getconf _NPROCESSORS_ONLN); cd ../..
echo -e "${YELLOW}Circle ${BLUE}built${NC}."

# We made it!
echo -e "${GREEN}${ORANGE}InstOS ${CYAN}Initialized${NC}."
