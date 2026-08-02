# Resolve the host tree root (metalpython / MicroPython) by walking up until
# tools/wasm_pack.py is found. Works whether this tree is used as
# extmod/wasmmod/examples or via a symlink at examples/wasmmod.
ifeq ($(TOP),)
TOP := $(shell d="$(CURDIR)"; while [ -n "$$d" ] && [ "$$d" != / ]; do \
	if [ -f "$$d/tools/wasm_pack.py" ]; then printf '%s' "$$d"; exit 0; fi; \
	d=$$(dirname "$$d"); \
	done; exit 1)
endif
ifeq ($(TOP),)
$(error cannot find host tree (tools/wasm_pack.py) walking up from $(CURDIR))
endif
