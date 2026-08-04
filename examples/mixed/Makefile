include ../find_top.mk
OUT = ../packs/mixed.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/shim.c src/lib.rs pack.toml $(WASM_PACK)
	mkdir -p ../packs
	python3 $(WASM_PACK) . -o $(OUT)
	rm -f $(OUT).zlib

clean:
	rm -f $(OUT) $(OUT).zlib .*.o
