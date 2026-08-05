# Build hello.wasm for the MicroPython wasm loader demo.
# For the ELF twin: make -C ../hello_elf
#
#   make -C extmod/wasmmod/examples/hello
#   make -C ports/unix MICROPY_PY_WASM=1

include ../find_top.mk
OUT = ../packs/hello.wasm

.PHONY: all clean

all: $(OUT)

$(OUT): src/hello.c src/__init__.py src/util/__init__.py src/util/extra.py pack.toml $(WASM_PACK)
	mkdir -p ../packs
	python3 $(WASM_PACK) pack . -o $(OUT)
	rm -f $(OUT).zlib

clean:
	rm -f $(OUT) $(OUT).zlib
