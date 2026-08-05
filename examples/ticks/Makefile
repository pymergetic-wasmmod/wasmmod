# Build ticks.wasm — positive micropython.runtime.ticks_ms catalog smoke.
include ../find_top.mk
OUT = ../packs/ticks.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/ticks.c pack.toml ../guest.h $(WASM_PACK)
	mkdir -p ../packs
	python3 $(WASM_PACK) pack . -o $(OUT)
	rm -f $(OUT).zlib

clean:
	rm -f $(OUT) $(OUT).zlib
