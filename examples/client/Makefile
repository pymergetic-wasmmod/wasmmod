# Build just this pack. The recipe lives in ../Makefile, which builds every
# example pack the same way.
#
#   make -C examples/client

.PHONY: all clean

all:
	$(MAKE) -C .. packs/pymergetic.wasmmod_examples.client.wasm

clean:
	rm -f ../packs/pymergetic.wasmmod_examples.client.wasm
