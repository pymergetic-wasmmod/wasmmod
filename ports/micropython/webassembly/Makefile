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

# Demo PKI root (sign-packs). Override MICROPY_WASM_TRUST_CA / MICROPY_WASM_VERIFY if needed.
SIGN_ROOT ?= $(WASMMOD_DIR)/examples/.keys/trust/root.crt.der
MICROPY_WASM_VERIFY ?= 1
MICROPY_WASM_TRUST_CA ?= $(SIGN_ROOT)

.PHONY: all clean sync-help ensure-trust
.DEFAULT_GOAL := all

ensure-trust:
	@if [ "$(MICROPY_WASM_VERIFY)" != "0" ] && [ -n "$(strip $(MICROPY_WASM_TRUST_CA))" ]; then \
		if [ ! -f "$(MICROPY_WASM_TRUST_CA)" ]; then \
			echo "==> gen demo PKI for TRUST_CA ($(MICROPY_WASM_TRUST_CA))"; \
			$(MAKE) -C $(WASMMOD_DIR)/examples sign-key; \
		fi; \
		test -f "$(MICROPY_WASM_TRUST_CA)" || { echo "FAIL: missing $(MICROPY_WASM_TRUST_CA)"; exit 1; }; \
	fi

all: ensure-trust
	$(MAKE) -C $(WEBASSEMBLY_PORT) \
		VARIANT_DIR=$(VARIANT_DIR) \
		BUILD=$(BUILD) \
		MICROPY_WASM_VERIFY=$(MICROPY_WASM_VERIFY) \
		MICROPY_WASM_TRUST_CA="$(MICROPY_WASM_TRUST_CA)" \
		-j$(shell nproc 2>/dev/null || echo 4)

clean:
	$(MAKE) -C $(WEBASSEMBLY_PORT) VARIANT_DIR=$(VARIANT_DIR) BUILD=$(BUILD) clean

sync-help:
	@echo "Sync into metal-cdn:"
	@echo "  ../../../../../metal-cdn/scripts/sync-repl-assets.sh $(BUILD)"
	@echo "Full local CDN (build + docker + seed samples):"
	@echo "  ../../../../../metal-cdn/scripts/dev-up.sh"
