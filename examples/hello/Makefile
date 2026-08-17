# Build just this pack. The recipe lives in ../Makefile, which builds every
# example pack the same way — two copies of it would drift the moment one pack
# needs a flag.
#
#   make -C examples/hello

.PHONY: all clean

all:
	$(MAKE) -C .. packs/pymergetic.wasmmod_examples.hello.wasm

clean:
	rm -f ../packs/pymergetic.wasmmod_examples.hello.wasm
