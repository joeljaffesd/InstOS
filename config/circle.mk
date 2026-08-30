# Circle BUILD configuration — compiled into the Circle libraries.
#
# This is the compile-time layer of config/: unlike cmdline.txt (read by
# Circle at runtime) and config-extra.txt (read by the GPU firmware at boot),
# changing anything here requires REBUILDING the Circle libs:
#
#   ./build-circle.sh
#
# init.sh generates external/circle/Config.mk as the host-specific toolchain
# PREFIX plus an include of this file.

AARCH = 64
RASPPI = 4

# CM4 has no VL805; its USB port hangs off the BCM2711's internal xHCI
# (pairs with otg_mode=1 in config.txt)
DEFINE += -DUSE_XHCI_INTERNAL

# Embedded assets (i.e. images or audio) push kernels past
# Circle's 2 MB default, which sysinit punishes with 
# a SILENT halt (rainbow screen, no serial). 
# Must be a multiple of 16 KB.
DEFINE += -DKERNEL_MAX_SIZE=0x800000
