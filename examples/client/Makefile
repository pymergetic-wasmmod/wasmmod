# Build client.wasm (imports hello.hello).
#
#   make -C examples/wasmmod/hello
#   make -C examples/wasmmod/client

TOP = ../../..
WASM_PACK = $(TOP)/tools/wasm_pack.py
OUT = client.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): client.c pack.toml ../guest.h $(WASM_PACK)
	python3 $(WASM_PACK) . -o $(OUT)

clean:
	rm -f $(OUT)
