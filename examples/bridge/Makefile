# Build bridge.wasm (depends on hello + mixed at runtime).
#
#   make -C extmod/wasmmod/examples/bridge

include ../find_top.mk
WASM_PACK = $(TOP)/tools/wasm_pack.py
OUT = bridge.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): bridge.c lib.rs py/__init__.py pack.toml ../guest.h $(WASM_PACK)
	python3 $(WASM_PACK) . -o $(OUT)

clean:
	rm -f $(OUT) .bridge.*.o
