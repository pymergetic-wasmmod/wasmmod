include ../find_top.mk
OUT = mixed.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/shim.c src/lib.rs pack.toml $(WASM_PACK)
	python3 $(WASM_PACK) . -o $(OUT)

clean:
	rm -f $(OUT) .*.o
