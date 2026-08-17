# Build just this pack. The recipe lives in ../Makefile, which builds every
# example pack the same way.
#
#   make -C examples/hostcall

.PHONY: all clean

all:
	$(MAKE) -C .. packs/pymergetic.wasmmod_examples.hostcall.elf

clean:
	rm -f ../packs/pymergetic.wasmmod_examples.hostcall.elf
