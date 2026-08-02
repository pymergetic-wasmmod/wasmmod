# Build client.wasm (imports hello.hello).
#
#   make -C extmod/wasmmod/examples/hello
#   make -C extmod/wasmmod/examples/client

include ../find_top.mk
WASM_PACK = $(TOP)/tools/wasm_pack.py
OUT = client.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): client.c pack.toml ../guest.h $(WASM_PACK)
	python3 $(WASM_PACK) . -o $(OUT)

clean:
	rm -f $(OUT)
