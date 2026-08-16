# MicroPython (upywm) make fragment for wasmmod.
# Included from extmod/extmod.mk when MICROPY_PY_WASM=1.
#
#   make -C ports/unix MICROPY_PY_WASM=1
#   make -C ports/webassembly MICROPY_PY_WASM=1 MICROPY_PY_METAL=1
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

# ports/webassembly sets CC=emcc after including extmod.mk.
ifeq ($(notdir $(CURDIR)),webassembly)
PM_WASMMOD_BROWSER := 1
endif

INC += -I$(WASMMOD_ABS) -I$(WASMMOD_ABS)/src

QSTR_DEFS += $(TOP)/$(WASMMOD_DIR)/ports/micropython/qstrdefs.wasmmod

ifdef PM_WASMMOD_BROWSER
# Browser cell: RS loader + WAMR interp (wasm32) + same µPy finder/hook as unix.
# Heap is util.mem (C) + tlsf; lock/registry/loader are the RS cards rustc'd
# for wasm32. Guest macros (PM_WASMMOD_GUEST=1) for C cards; instantiate is
# still the host loader linked into this image.
MICROPY_PY_WASM_GEN := 0
MICROPY_PY_WASM_ELF := 0
MICROPY_WASM_VERIFY ?= 0
MICROPY_WASM_AOT_VERSION ?= 0
MICROPY_WASM_CONTAINERS ?= wasm

CFLAGS_EXTMOD += -include $(WASMMOD_ABS)/ports/micropython/mpconfig_wasm.h \
	-DMICROPY_PY_WASM=1 \
	-DMICROPY_MODULE_BUILTIN_SUBPACKAGES=1 \
	-DMICROPY_MODULE_BUILTIN_INIT=1 \
	-DMICROPY_CAN_OVERRIDE_BUILTINS=1 \
	-DPM_WASMMOD_GUEST=1 \
	-DMICROPY_WASM_HTTP_NATIVE=0 \
	-DMICROPY_PY_WASM_GEN=0 \
	-DMICROPY_PY_WASM_ELF=0 \
	-DMICROPY_WASM_VERIFY=$(MICROPY_WASM_VERIFY) \
	-DMICROPY_WASM_AOT_VERSION=$(MICROPY_WASM_AOT_VERSION) \
	-DMICROPY_WASM_CONTAINERS=\"$(MICROPY_WASM_CONTAINERS)\"

SRC_WASMMOD = \
	$(WASMMOD_DIR)/src/pymergetic/util/mem/__impl__.c \
	$(WASMMOD_DIR)/third_party/tlsf/tlsf.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/boot/__impl__.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/io/__impl__.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/net/cdn/__impl__.c \
	$(WASMMOD_DIR)/ports/webassembly/io_browser.c \
	$(WASMMOD_DIR)/ports/common/boot.c \
	$(WASMMOD_DIR)/ports/common/load.c \
	$(WASMMOD_DIR)/ports/common/memcookie.c \
	$(WASMMOD_DIR)/ports/micropython/modguest.c \
	$(WASMMOD_DIR)/ports/micropython/modcdn.c \
	$(WASMMOD_DIR)/ports/micropython/importhook.c \
	$(WASMMOD_DIR)/ports/micropython/finder.c \
	$(WASMMOD_DIR)/ports/micropython/packbind.c \
	$(WASMMOD_DIR)/ports/micropython/nativecall.c \
	$(WASMMOD_DIR)/ports/micropython/objhandle.c \
	$(WASMMOD_DIR)/ports/micropython/hostready.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pyexport/__impl__.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/manifest.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/zlib_env.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/common/format.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/wasm/section.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/aot/section.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/elf/section.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/verify/__impl__.c \
	extmod/metal/port/wamr/metal_platform.c

PY_O += $(addprefix $(BUILD)/, $(SRC_WASMMOD:.c=.o))
SRC_QSTR += $(filter-out extmod/metal/port/wamr/metal_platform.c,$(SRC_WASMMOD))

$(addprefix $(BUILD)/, $(SRC_WASMMOD:.c=.o)): CFLAGS += -std=gnu99

$(BUILD)/extmod/metal/port/wamr/metal_platform.o: CFLAGS += \
	-I$(TOP)/extmod/metal/port/wamr \
	-I$(WASMMOD_ABS)/third_party/wamr/core/iwasm/include \
	-I$(WASMMOD_ABS)/third_party/wamr/core/shared/platform/include \
	-I$(WASMMOD_ABS)/third_party/wamr/core \
	-DBH_PLATFORM_METAL -DBUILD_TARGET_X86_32 \
	-DWASM_ENABLE_INTERP=1 -DWASM_ENABLE_FAST_INTERP=1 \
	-DWASM_ENABLE_SHARED_HEAP=1 -DWASM_DISABLE_HW_BOUND_CHECK=1 \
	-D_PLATFORM_WASI_TYPES_H

# Same RS lock/registry/loader/version as firmware.
WASMMOD_LOCK_A := $(BUILD)/libfw_lock.a
$(WASMMOD_LOCK_A): $(TOP)/extmod/metal/port/fw_lock/lib.rs | $(BUILD)
	$(ECHO) "RUSTC loader wasm32"
	$(Q)rustc --edition 2024 --crate-type staticlib --crate-name fw_lock \
		--target wasm32-unknown-unknown -C panic=abort -C opt-level=s \
		-o $@ $<

WASMMOD_WAMR_A := $(BUILD)/libwasmmod_wamr_freestanding.a
$(WASMMOD_WAMR_A): | $(BUILD)
	$(ECHO) "WAMR emcc interp"
	$(Q)$(MAKE) -f $(WASMMOD_ABS)/ports/metal/wamr_freestanding.mk \
		EMCC=1 OUT_DIR=$(BUILD)/wamr \
		METAL_PLAT_INC=$(TOP)/extmod/metal/port/wamr \
		METAL_PORT_INC=$(TOP)/extmod/metal/port \
		METAL_LIBC_INC=$(TOP)/extmod/metal/port/wamr \
		METAL_SRC_INC=$(TOP)/extmod/metal/src \
		METAL_INCLUDE_INC=$(TOP)/extmod/metal/src \
		WASMMOD_DIR=$(WASMMOD_ABS)
	$(Q)cp $(BUILD)/wamr/libwasmmod_wamr_freestanding.a $@

$(BUILD)/micropython.mjs: $(WASMMOD_LOCK_A) $(WASMMOD_WAMR_A)
LDFLAGS_EXTMOD += $(WASMMOD_LOCK_A) $(WASMMOD_WAMR_A)

else

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
# --no-default-features: drop bundle-mbedtls; unix already compiles lib/mbedtls.
ifeq ($(WASMMOD_CARGO_FEATURES),)
ifeq ($(MICROPY_PY_WASM_GEN),1)
WASMMOD_CARGO_FEATURES := upy-host,gen
else
WASMMOD_CARGO_FEATURES := upy-host
endif
endif

CFLAGS_EXTMOD += -include $(WASMMOD_ABS)/ports/micropython/mpconfig_wasm.h \
	-DMICROPY_PY_WASM=1 \
	-DMICROPY_MODULE_BUILTIN_SUBPACKAGES=1 \
	-DMICROPY_MODULE_BUILTIN_INIT=1 \
	-DPM_WASMMOD_GUEST=0 \
	-DMICROPY_PY_WASM_GEN=$(MICROPY_PY_WASM_GEN) \
	-DMICROPY_PY_WASM_ELF=$(MICROPY_PY_WASM_ELF) \
	-DMICROPY_WASM_VERIFY=$(MICROPY_WASM_VERIFY) \
	-DMICROPY_WASM_AOT_VERSION=$(MICROPY_WASM_AOT_VERSION) \
	-DMICROPY_WASM_CONTAINERS=\"$(MICROPY_WASM_CONTAINERS)\" \
	-DMICROPY_WASMMOD_HOST_SRC=\"$(WASMMOD_ABS)/src\"

SRC_WASMMOD = \
	$(WASMMOD_DIR)/ports/common/boot.c \
	$(WASMMOD_DIR)/ports/common/load.c \
	$(WASMMOD_DIR)/ports/common/memcookie.c \
	$(WASMMOD_DIR)/ports/micropython/modwasmmod.c \
	$(WASMMOD_DIR)/ports/micropython/modcdn.c \
	$(WASMMOD_DIR)/ports/micropython/modguest.c \
	$(WASMMOD_DIR)/ports/micropython/modutil.c \
	$(WASMMOD_DIR)/ports/micropython/importhook.c \
	$(WASMMOD_DIR)/ports/micropython/hostready.c \
	$(WASMMOD_DIR)/ports/micropython/nativecall.c \
	$(WASMMOD_DIR)/ports/micropython/objhandle.c \
	$(WASMMOD_DIR)/ports/micropython/finder.c \
	$(WASMMOD_DIR)/ports/micropython/packbind.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pyexport/__impl__.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/manifest.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/zlib_env.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/source.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/common/format.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/wasm/section.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/aot/section.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/elf/section.c \
	$(WASMMOD_DIR)/src/pymergetic/wasmmod/verify/__impl__.c

ifeq ($(MICROPY_PY_WASM_GEN),1)
SRC_WASMMOD += $(WASMMOD_DIR)/ports/micropython/modgen.c
endif

ifeq ($(MICROPY_PY_WASM_ELF),1)
SRC_WASMMOD += $(WASMMOD_DIR)/src/pymergetic/wasmmod/pack/format/elf/load.c
endif

PY_O += $(addprefix $(BUILD)/, $(SRC_WASMMOD:.c=.o))
SRC_QSTR += $(SRC_WASMMOD)

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
	$(Q)cd $(WASMMOD_ABS) && cargo build --lib --release --no-default-features --features $(WASMMOD_CARGO_FEATURES)
.PHONY: FORCE
FORCE:

# Ensure the staticlib exists before linking the unix port binary.
$(BUILD)/firmware.elf: $(WASMMOD_STATICLIB)
$(BUILD)/micropython: $(WASMMOD_STATICLIB)

LDFLAGS_EXTMOD += -L$(WASMMOD_CARGO_TARGET) -lpymergetic_wasmmod -lpthread -ldl -lm -lstdc++
# io HTTPS leaves U mbedtls_*; unix already compiles lib/mbedtls (ssl).
# Cargo --no-default-features drops bundle-mbedtls so this .a is not a second copy.
# WAMR vmlib is a separate static lib (build.rs); locate it under cargo OUT_DIR.
WASMMOD_IWASM_A := $(firstword $(wildcard $(WASMMOD_ABS)/target/release/build/*/out/vmlib/build/libiwasm.a))
ifneq ($(WASMMOD_IWASM_A),)
LDFLAGS_EXTMOD += -L$(dir $(WASMMOD_IWASM_A)) -liwasm
endif

endif
