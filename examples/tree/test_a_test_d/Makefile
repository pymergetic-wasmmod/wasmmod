include ../../find_top.mk
OUT = ../../packs/pymergetic.wasmmod_examples.test_a.test_d.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/shim.c src/lib.rs src/__init__.py pack.toml $(WASM_PACK)
	mkdir -p ../../packs
	python3 $(WASM_PACK) pack . -o $(OUT)
	rm -f $(OUT).zlib

clean:
	rm -f $(OUT) $(OUT).zlib .*.o
