# Resolve:
#   WASMMOD_ROOT — this repo (has micropython.mk + tools/wasm_pack.py)
#   TOP            — host tree (metalpython / MicroPython: ports/unix + py/)
# Works from extmod/wasmmod/examples or a symlink at examples/wasmmod.

ifeq ($(WASMMOD_ROOT),)
WASMMOD_ROOT := $(shell d="$(CURDIR)"; while [ -n "$$d" ] && [ "$$d" != / ]; do \
	if [ -f "$$d/micropython.mk" ] && [ -f "$$d/tools/wasm_pack.py" ]; then \
		printf '%s' "$$d"; exit 0; \
	fi; \
	d=$$(dirname "$$d"); \
	done; exit 1)
endif
ifeq ($(WASMMOD_ROOT),)
$(error cannot find wasmmod root (micropython.mk + tools/wasm_pack.py) from $(CURDIR))
endif

ifeq ($(TOP),)
TOP := $(shell d="$(CURDIR)"; while [ -n "$$d" ] && [ "$$d" != / ]; do \
	if [ -f "$$d/ports/unix/Makefile" ] && [ -f "$$d/py/mpconfig.h" ]; then \
		printf '%s' "$$d"; exit 0; \
	fi; \
	d=$$(dirname "$$d"); \
	done; exit 1)
endif
ifeq ($(TOP),)
$(error cannot find host tree (ports/unix + py/mpconfig.h) from $(CURDIR))
endif

WASM_PACK ?= $(WASMMOD_ROOT)/tools/wasm_pack.py
