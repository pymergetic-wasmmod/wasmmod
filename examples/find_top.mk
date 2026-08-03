# Resolve trees from any examples/*/ Makefile:
#   WASMMOD_ROOT — this repo (ports/micropython/micropython.mk + tools/wasmmod.py)
#   METALPYTHON_TOP — metalpython checkout when wasmmod is a submodule (optional)
_d := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
WASMMOD_ROOT := $(shell \
	d="$(_d)"; \
	while [ "$$d" != / ]; do \
	if [ -f "$$d/ports/micropython/micropython.mk" ] && [ -f "$$d/tools/wasmmod.py" ]; then \
		echo "$$d"; exit 0; \
	fi; \
	d=$$(dirname "$$d"); \
	done; \
	exit 1)
ifeq ($(WASMMOD_ROOT),)
$(error cannot find wasmmod root (ports/micropython/micropython.mk + tools/wasmmod.py) from $(CURDIR))
endif

# When examples live under metalpython/extmod/wasmmod/…, TOP is metalpython.
# Standalone wasmmod clone: no metalpython parent — leave empty; host builds need TOP.
METALPYTHON_TOP := $(shell \
	p="$(WASMMOD_ROOT)"; \
	if [ "$$(basename "$$p")" = wasmmod ] && [ "$$(basename "$$(dirname "$$p")")" = extmod ]; then \
		dirname "$$(dirname "$$p")"; \
	fi)

TOP ?= $(METALPYTHON_TOP)
WASMMOD ?= $(WASMMOD_ROOT)/tools/wasmmod.py
WASM_PACK ?= $(WASMMOD_ROOT)/tools/wasmmod_pack.py
WASM_SIGN ?= $(WASMMOD_ROOT)/tools/wasmmod_sign.py
