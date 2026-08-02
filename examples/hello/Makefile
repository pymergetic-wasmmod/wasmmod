# Build hello.wasm for the MicroPython wasm loader demo.
#
#   make -C examples/wasmmod/hello
#   make -C ports/unix MICROPY_PY_WASM=1
#   ports/unix/build-standard/micropython -c \
#     'import wasm; m=wasm.load_pack("examples/wasmmod/hello/hello.wasm"); print(m.hello(), m.greet())'
#
# Pack format: see ../PACK.md

TOP = ../../..
WASM_PACK = $(TOP)/tools/wasm_pack.py
OUT = hello.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): hello.c py/__init__.py py/util/__init__.py py/util/extra.py pack.toml $(WASM_PACK)
	python3 $(WASM_PACK) . -o $(OUT)

clean:
	rm -f $(OUT)
