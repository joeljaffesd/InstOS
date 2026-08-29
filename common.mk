# Shared build configuration for InstOS examples.
#
# An example is a directory with a main.cpp and a two-line Makefile:
#   include ../../common.mk
#
# All examples build into the repo root's build/ directory (one active image),
# which is what devloop.sh flashes.

ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

CIRCLEHOME = $(ROOT)/external/circle
BUILDDIR   = $(ROOT)/build

OBJS = $(BUILDDIR)/main.o

LIBS	= $(CIRCLEHOME)/lib/sound/libsound.a \
	  $(CIRCLEHOME)/lib/usb/libusb.a \
	  $(CIRCLEHOME)/lib/input/libinput.a \
	  $(CIRCLEHOME)/lib/fs/libfs.a \
	  $(CIRCLEHOME)/lib/net/libnet.a \
	  $(CIRCLEHOME)/lib/sched/libsched.a \
	  $(CIRCLEHOME)/lib/libcircle.a

# external/barebone provides <Barebone.hpp> (the cuttlebone wire protocol);
# InstOS.hpp and modules/ live at the repo root. The root comes LAST so a
# stray copy of a submodule header sitting there cannot shadow the real one.
INCLUDE += -I $(ROOT)/external/barebone -I $(ROOT)/external/MicroNAM -I $(ROOT)

include $(CIRCLEHOME)/Rules.mk

# Put the kernel image (and .elf/.map/.lst) in build/ too.
# Must come after the include so Rules.mk has computed the default name.
TARGET := $(BUILDDIR)/$(notdir $(TARGET))

# Out-of-tree compile rules; mirror Rules.mk's recipes.
# make runs inside the example dir, so the stem resolves to its main.cpp.
$(BUILDDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  CPP   $@"
	@$(CPP) $(CPPFLAGS) -c -o $@ $<

$(BUILDDIR)/%.d: %.cpp
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) -M -MG -MT $(@:.d=.o) -MT $@ -MF $@ $<

# Intel-hex image for serial upload via the bootloader (devloop.sh).
# Rules.mk's own .hex rule was parsed before the TARGET remap, so redo it here.
$(TARGET).hex: $(TARGET).img
	@echo "  COPY  $@"
	@$(OBJCOPY) $(TARGET).elf -O ihex $@

distclean:
	rm -rf $(BUILDDIR)

-include $(DEPS)
