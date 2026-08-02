# Build hello.wasm for the MicroPython wasm loader demo.
#
#   make -C extmod/wasmmod/examples/hello
#   make -C ports/unix MICROPY_PY_WASM=1
#
# Pack format: see ../PACK.md

include ../find_top.mk
OUT = hello.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): hello.c py/__init__.py py/util/__init__.py py/util/extra.py pack.toml $(WASM_PACK)
	python3 $(WASM_PACK) . -o $(OUT)

clean:
	rm -f $(OUT)
