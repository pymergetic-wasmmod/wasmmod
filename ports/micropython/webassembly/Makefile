# Build MicroPython webassembly + wasmmod (no metalpython tree pollution)
#
# Usage (from anywhere; TOP = metalpython root that has this submodule):
#   make -C extmod/wasmmod/ports/micropython/webassembly
#   # → $(TOP)/ports/webassembly/build-wasmmod/micropython.{mjs,wasm}
#
# Or with explicit TOP:
#   make -C …/webassembly TOP=/path/to/metalpython

WASMMOD_WEBASSEMBLY_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
WASMMOD_DIR := $(abspath $(WASMMOD_WEBASSEMBLY_DIR)/../../..)
# submodule at extmod/wasmmod → TOP is parent of extmod
TOP ?= $(abspath $(WASMMOD_DIR)/../..)
WEBASSEMBLY_PORT := $(TOP)/ports/webassembly
VARIANT_DIR := $(WASMMOD_WEBASSEMBLY_DIR)/variant
BUILD ?= $(WEBASSEMBLY_PORT)/build-wasmmod

.PHONY: all clean sync-help
all:
	$(MAKE) -C $(WEBASSEMBLY_PORT) \
		VARIANT_DIR=$(VARIANT_DIR) \
		BUILD=$(BUILD) \
		-j$(shell nproc 2>/dev/null || echo 4)

clean:
	$(MAKE) -C $(WEBASSEMBLY_PORT) VARIANT_DIR=$(VARIANT_DIR) BUILD=$(BUILD) clean

sync-help:
	@echo "Sync into metal-cdn:"
	@echo "  ../../../../../metal-cdn/scripts/sync-repl-assets.sh $(BUILD)"
	@echo "Full local CDN (build + docker + seed samples):"
	@echo "  ../../../../../metal-cdn/scripts/dev-up.sh"
