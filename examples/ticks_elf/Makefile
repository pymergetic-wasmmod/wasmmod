# Build just this pack. The recipe lives in ../Makefile, which builds every
# example pack the same way.
#
#   make -C examples/ticks_elf

.PHONY: all clean

all:
	$(MAKE) -C .. packs/pymergetic.wasmmod_examples.ticks.elf

clean:
	rm -f ../packs/pymergetic.wasmmod_examples.ticks.elf
