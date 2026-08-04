include ../../find_top.mk
OUT = ../../packs/test_a.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/shim.c src/lib.rs src/__init__.py pack.toml $(WASM_PACK)
	mkdir -p ../../packs
	python3 $(WASM_PACK) . -o $(OUT)

clean:
	rm -f $(OUT) .*.o
