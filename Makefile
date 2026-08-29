# Delegates to the selected example (Line 9).
#
#   make                                # build the default example
#   make EXAMPLE=examples/blink         # build another example
#   make build/kernel8-rpi4.hex         # hex image for devloop.sh
#
# Shared build config lives in common.mk; each example dir just includes it.

EXAMPLE ?= examples/blink

.PHONY: all distclean

all:
	@$(MAKE) -C $(EXAMPLE)

build/%.img build/%.hex:
	@$(MAKE) -C $(EXAMPLE) $(CURDIR)/$@

distclean:
	rm -rf build