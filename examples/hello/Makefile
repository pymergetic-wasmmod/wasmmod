# Build hello.wasm for the MicroPython wasm loader demo.
#
#   make -C extmod/wasmmod/examples/hello
#   make -C ports/unix MICROPY_PY_WASM=1
#
# Pack format: see ../PACK.md

include ../find_top.mk
OUT = ../packs/hello.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/hello.c src/__init__.py src/util/__init__.py src/util/extra.py pack.toml $(WASM_PACK)
	mkdir -p ../packs
	python3 $(WASM_PACK) . -o $(OUT)

clean:
	rm -f $(OUT)
