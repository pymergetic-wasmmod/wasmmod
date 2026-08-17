# Build just this pack. The recipe lives in ../Makefile, which builds every
# example pack the same way.
#
#   make -C examples/mixed

.PHONY: all clean

all:
	$(MAKE) -C .. packs/pymergetic.wasmmod_examples.mixed.wasm

clean:
	rm -f ../packs/pymergetic.wasmmod_examples.mixed.wasm
