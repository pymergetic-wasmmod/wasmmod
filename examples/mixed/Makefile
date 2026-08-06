include ../find_top.mk
OUT = ../packs/pymergetic.wasmmod_examples.mixed.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/shim.c src/lib.rs pack.toml $(WASM_PACK)
	mkdir -p ../packs
	python3 $(WASM_PACK) pack . -o $(OUT)
	rm -f $(OUT).zlib

clean:
	rm -f $(OUT) $(OUT).zlib .*.o
