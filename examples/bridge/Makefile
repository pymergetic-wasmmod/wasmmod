# Build bridge.wasm (depends on hello + mixed at runtime).
#
#   make -C extmod/wasmmod/examples/bridge

include ../find_top.mk
OUT = ../packs/bridge.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/bridge.c src/lib.rs src/__init__.py pack.toml ../guest.h $(WASM_PACK)
	mkdir -p ../packs
	python3 $(WASM_PACK) . -o $(OUT)

clean:
	rm -f $(OUT) .bridge.*.o
