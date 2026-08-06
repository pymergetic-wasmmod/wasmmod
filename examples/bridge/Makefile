# Build bridge.wasm (depends on hello + mixed at runtime).
#
#   make -C extmod/wasmmod/examples/bridge

include ../find_top.mk
OUT = ../packs/pymergetic.wasmmod_examples.bridge.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/bridge.c src/lib.rs src/__init__.py pack.toml $(WASMMOD_INCLUDE)/pm_guest.h $(WASM_PACK)
	mkdir -p ../packs
	python3 $(WASM_PACK) pack . -o $(OUT)
	rm -f $(OUT).zlib

clean:
	rm -f $(OUT) $(OUT).zlib .bridge.*.o
