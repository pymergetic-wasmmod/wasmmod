# Build client.wasm (imports hello.hello).
#
#   make -C extmod/wasmmod/examples/hello
#   make -C extmod/wasmmod/examples/client

include ../find_top.mk
OUT = client.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/client.c pack.toml ../guest.h $(WASM_PACK)
	python3 $(WASM_PACK) . -o $(OUT)

clean:
	rm -f $(OUT)
