# MicroPython (mpwm) make fragment for wasmmod.
# Included from extmod/extmod.mk when MICROPY_PY_WASM=1.
#
#   make -C ports/unix MICROPY_PY_WASM=1
#
# Thin µPy face over the Rust registry+loader staticlib. No resurrected
# host_slots / call0_py — every native resolve goes through
# pm_wasmmod_registry_resolve_native.

WASMMOD_DIR ?= extmod/wasmmod
WASMMOD_ABS := $(TOP)/$(WASMMOD_DIR)
# Honour CARGO_TARGET_DIR when set (CI/sandbox); else crate-local target/.
WASMMOD_CARGO_TARGET := $(shell cd $(WASMMOD_ABS) && cargo metadata --no-deps --format-version 1 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['target_directory']+'/release')")
ifeq ($(WASMMOD_CARGO_TARGET),)
WASMMOD_CARGO_TARGET := $(WASMMOD_ABS)/target/release
endif
WASMMOD_STATICLIB := $(WASMMOD_CARGO_TARGET)/libpymergetic_wasmmod.a

INC += -I$(WASMMOD_ABS)

CFLAGS_EXTMOD += -DMICROPY_PY_WASM=1 \
	-DMICROPY_MODULE_BUILTIN_SUBPACKAGES=1 \
	-DMICROPY_MODULE_BUILTIN_INIT=1 \
	-DPM_WASMMOD_GUEST=0

SRC_WASMMOD = \
	$(WASMMOD_DIR)/ports/micropython/modwasmmod.c

PY_O += $(addprefix $(BUILD)/, $(SRC_WASMMOD:.c=.o))
SRC_QSTR += $(SRC_WASMMOD)
QSTR_DEFS += $(TOP)/$(WASMMOD_DIR)/ports/micropython/qstrdefs.wasmmod

# Nested WAMR for cargo build.rs / submodules.
GIT_SUBMODULES += $(WASMMOD_DIR)
submodules: sync-wasmmod-nested
.PHONY: sync-wasmmod-nested
sync-wasmmod-nested:
	$(Q)cd $(TOP) && git submodule update --init --recursive $(WASMMOD_DIR)

.PHONY: wasmmod-staticlib
wasmmod-staticlib: $(WASMMOD_STATICLIB)

$(WASMMOD_STATICLIB):
	$(ECHO) "CARGO $(WASMMOD_DIR) (upy-host staticlib)"
	$(Q)cd $(WASMMOD_ABS) && cargo build --release --features upy-host

# Ensure the staticlib exists before linking the unix port binary.
$(BUILD)/firmware.elf: $(WASMMOD_STATICLIB)
$(BUILD)/micropython: $(WASMMOD_STATICLIB)

LDFLAGS_EXTMOD += -L$(WASMMOD_CARGO_TARGET) -lpymergetic_wasmmod -lpthread -ldl -lm -lstdc++
# WAMR vmlib is a separate static lib (build.rs); locate it under cargo OUT_DIR.
WASMMOD_IWASM_A := $(firstword $(wildcard $(WASMMOD_ABS)/target/release/build/*/out/vmlib/build/libiwasm.a))
ifneq ($(WASMMOD_IWASM_A),)
LDFLAGS_EXTMOD += -L$(dir $(WASMMOD_IWASM_A)) -liwasm
endif
