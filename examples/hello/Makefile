# Build hello.wasm via the *.pmm.toml-based packer (dev/tools/) — mirrors
# the old pack.toml-based examples/hello/Makefile, just pointed at the new
# card tree and packer. No MicroPython/run_matrix.py verification wiring
# yet (that host tree doesn't exist in this destination tree yet); see
# the pymergetic-wasmmod crate's own loader tests for a real proof.
#
#   make -C examples/hello

TOOLS_SRC := ../../dev/tools/src
OUT = ../packs/pymergetic.wasmmod_examples.hello.wasm
CARD_DIR = src/pymergetic/wasmmod_examples/hello

.PHONY: all clean

all: $(OUT)

$(OUT): $(CARD_DIR)/__pmm__.toml $(CARD_DIR)/__impl__.c mount/__init__.py mount/util/__init__.py mount/util/extra.py
	mkdir -p ../packs
	PYTHONPATH=$(TOOLS_SRC) python3 -m pymergetic.wasmmod.tools pack $(CARD_DIR) -o $(OUT)

clean:
	rm -f $(OUT)
