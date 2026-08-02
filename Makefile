CIRCLEHOME = external/circle
BUILDDIR   = build

OBJS = $(BUILDDIR)/src/main.o $(BUILDDIR)/src/kernel/kernel.o

LIBS	= $(CIRCLEHOME)/lib/libcircle.a

INCLUDE += -I src/kernel

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

distclean:
	rm -rf $(BUILDDIR)

-include $(DEPS)
