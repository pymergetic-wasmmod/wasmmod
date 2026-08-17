# Generated card faces, produced before anything compiles.
#
# Include this from every seat that compiles cards — wasmmod's own µPy port and
# every seat of a downstream card tree. Generation runs at parse time on purpose:
# the faces have to
# exist before the first .o, and a stamp keeps it a no-op once nothing has
# changed. See tools/genfaces.sh.
#
# A downstream card tree sets WASMMOD_GEN_ROOTS to its own src/ before including
# this, so wasmmod never carries a list of who depends on it.

ifndef WASMMOD_GEN_MK
WASMMOD_GEN_MK := 1

WASMMOD_GEN_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))

ifneq ($(MAKECMDGOALS),clean)
WASMMOD_GEN_FAIL := $(shell $(WASMMOD_GEN_DIR)/tools/genfaces.sh $(WASMMOD_GEN_ROOTS) >/dev/null || echo fail)
ifneq ($(WASMMOD_GEN_FAIL),)
$(error card face generation failed — run $(WASMMOD_GEN_DIR)/tools/genfaces.sh)
endif
endif

endif
