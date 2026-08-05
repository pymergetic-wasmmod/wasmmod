# Build client.wasm (imports hello.hello).
#
#   make -C extmod/wasmmod/examples/hello
#   make -C extmod/wasmmod/examples/client

include ../find_top.mk
OUT = ../packs/client.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/client.c pack.toml ../guest.h $(WASM_PACK)
	mkdir -p ../packs
	python3 $(WASM_PACK) pack . -o $(OUT)
	rm -f $(OUT).zlib

clean:
	rm -f $(OUT) $(OUT).zlib
