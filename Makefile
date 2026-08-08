CIRCLEHOME = external/circle
BUILDDIR   = build

OBJS = $(BUILDDIR)/src/main.o

LIBS	= $(CIRCLEHOME)/lib/libcircle.a

INCLUDE += -I .

include $(CIRCLEHOME)/Rules.mk

# Put the kernel image (and .elf/.map/.lst) in build/ too.
# Must come after the include so Rules.mk has computed the default name.
TARGET := $(BUILDDIR)/$(notdir $(TARGET))

# Out-of-tree compile rules; mirror Rules.mk's recipes.
# Stem trick: build/src/main.o -> stem "src/main" -> src/main.cpp
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
