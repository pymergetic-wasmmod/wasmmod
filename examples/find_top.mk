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
# Standalone wasmmod clone / pip share tree: TOP empty — host pack/sign still work;
# firmware / test-engines need a MicroPython tree (set TOP=…).
METALPYTHON_TOP := $(shell \
	p="$(WASMMOD_ROOT)"; \
	if [ "$$(basename "$$p")" = wasmmod ] && [ "$$(basename "$$(dirname "$$p")")" = extmod ]; then \
		dirname "$$(dirname "$$p")"; \
	fi)

TOP ?= $(METALPYTHON_TOP)
# Host CLI: tools/wasmmod.py → pymergetic-wasmmod-tools (pip install --pre pymergetic-wasmmod).
# Keep these as a *single path* — Make treats spaces in prereqs as extra targets.
WASMMOD ?= $(WASMMOD_ROOT)/tools/wasmmod.py
WASM_PACK ?= $(WASMMOD)
WASM_SIGN ?= $(WASMMOD)
# In-tree tools (no pip / PEP-668). Shim also adds this path.
export PYTHONPATH := $(WASMMOD_ROOT)/dev/tools/src$(if $(PYTHONPATH),:$(PYTHONPATH),)
# Public compile surface (pm_guest.h, pm_upy/*, pm_wasmmod/*).
WASMMOD_INCLUDE ?= $(WASMMOD_ROOT)/include
# ELF guests: no __wasm__ — force guest import prototypes.
GUEST_CFLAGS ?= -I$(WASMMOD_INCLUDE) -DPM_WASMMOD_GUEST=1
