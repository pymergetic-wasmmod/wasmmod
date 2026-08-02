include ../find_top.mk
OUT = mixed.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): shim.c lib.rs pack.toml $(WASM_PACK)
	python3 $(WASM_PACK) . -o $(OUT)

clean:
	rm -f $(OUT) .*.o
