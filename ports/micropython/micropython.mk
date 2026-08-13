# MicroPython (mpwm) make fragment for wasmmod.
# Included from extmod/extmod.mk when MICROPY_PY_WASM=1.
#
#   make -C ports/unix MICROPY_PY_WASM=1
#
# Menuconfig surface: ports/micropython/mpconfig_wasm.h
# Make forwards (override any of these on the command line):
#   MICROPY_PY_WASM_GEN          in-bin/host util.gen (default 1 on unix)
#   MICROPY_PY_WASM_ELF          ELF containers (default 1)
#   MICROPY_WASM_VERIFY          pack verify (default 0)
#   MICROPY_WASM_AOT_VERSION
#   MICROPY_WASM_CONTAINERS      elf,aot,wasm
#   WASMMOD_CARGO_FEATURES       overrides cargo feature list entirely

WASMMOD_DIR ?= extmod/wasmmod
WASMMOD_ABS := $(TOP)/$(WASMMOD_DIR)
# Honour CARGO_TARGET_DIR when set (CI/sandbox); else crate-local target/.
WASMMOD_CARGO_TARGET := $(shell cd $(WASMMOD_ABS) && cargo metadata --no-deps --format-version 1 2>/dev/null | python3 -c "import sys,json; print(json.load(sys.stdin)['target_directory']+'/release')")
ifeq ($(WASMMOD_CARGO_TARGET),)
WASMMOD_CARGO_TARGET := $(WASMMOD_ABS)/target/release
endif
WASMMOD_STATICLIB := $(WASMMOD_CARGO_TARGET)/libpymergetic_wasmmod.a

MICROPY_PY_WASM_GEN ?= 1
MICROPY_PY_WASM_ELF ?= 1
MICROPY_WASM_VERIFY ?= 0
MICROPY_WASM_AOT_VERSION ?= 0
MICROPY_WASM_CONTAINERS ?= elf,aot,wasm

# Cargo: gen/build-machinery only when MICROPY_PY_WASM_GEN=1.
ifeq ($(WASMMOD_CARGO_FEATURES),)
ifeq ($(MICROPY_PY_WASM_GEN),1)
WASMMOD_CARGO_FEATURES := upy-host,gen
else
WASMMOD_CARGO_FEATURES := upy-host
endif
endif

INC += -I$(WASMMOD_ABS) -I$(WASMMOD_ABS)/src

CFLAGS_EXTMOD += -include $(WASMMOD_ABS)/ports/micropython/mpconfig_wasm.h \
	-DMICROPY_PY_WASM=1 \
	-DMICROPY_MODULE_BUILTIN_SUBPACKAGES=1 \
	-DMICROPY_MODULE_BUILTIN_INIT=1 \
	-DPM_WASMMOD_GUEST=0 \
	-DMICROPY_PY_WASM_GEN=$(MICROPY_PY_WASM_GEN) \
	-DMICROPY_PY_WASM_ELF=$(MICROPY_PY_WASM_ELF) \
	-DMICROPY_WASM_VERIFY=$(MICROPY_WASM_VERIFY) \
	-DMICROPY_WASM_AOT_VERSION=$(MICROPY_WASM_AOT_VERSION) \
	-DMICROPY_WASM_CONTAINERS=\"$(MICROPY_WASM_CONTAINERS)\"

SRC_WASMMOD = \
	$(WASMMOD_DIR)/ports/micropython/modwasmmod.c \
	$(WASMMOD_DIR)/ports/micropython/finder.c \
	$(WASMMOD_DIR)/ports/micropython/packbind.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/manifest.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/zlib_env.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/source.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/common/format.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/wasm/section.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/aot/section.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/elf/section.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/verify/__impl__.c

ifeq ($(MICROPY_PY_WASM_ELF),1)
SRC_WASMMOD += $(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/elf/load.c
endif

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

# Always ask cargo — it decides whether sources are stale (make cannot
# track the Rust graph). Touch the archive when cargo reports work done.
$(WASMMOD_STATICLIB): FORCE
	$(ECHO) "CARGO $(WASMMOD_DIR) ($(WASMMOD_CARGO_FEATURES) staticlib)"
	$(Q)cd $(WASMMOD_ABS) && cargo build --release --features $(WASMMOD_CARGO_FEATURES)
.PHONY: FORCE
FORCE:

# Ensure the staticlib exists before linking the unix port binary.
$(BUILD)/firmware.elf: $(WASMMOD_STATICLIB)
$(BUILD)/micropython: $(WASMMOD_STATICLIB)

LDFLAGS_EXTMOD += -L$(WASMMOD_CARGO_TARGET) -lpymergetic_wasmmod -lpthread -ldl -lm -lstdc++
# WAMR vmlib is a separate static lib (build.rs); locate it under cargo OUT_DIR.
WASMMOD_IWASM_A := $(firstword $(wildcard $(WASMMOD_ABS)/target/release/build/*/out/vmlib/build/libiwasm.a))
ifneq ($(WASMMOD_IWASM_A),)
LDFLAGS_EXTMOD += -L$(dir $(WASMMOD_IWASM_A)) -liwasm
endif
