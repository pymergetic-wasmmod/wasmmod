# MicroPython / metalpython make fragment for wasmmod + nested WAMR.
# Included from extmod/extmod.mk when MICROPY_PY_WASM=1.
#
# Enable from the command line (no mpconfig.h / mpconfigport.mk edits needed):
#   make MICROPY_PY_WASM=1
# Optional: MICROPY_PY_WASM_{AOT,ELF,JIT,FAST_JIT,MATRIX} MICROPY_WASM_VERIFY
#           MICROPY_WASM_CONTAINERS=elf,aot,wasm
# Bake host root CA(s) into the image (public DER only):
#   make MICROPY_WASM_VERIFY=1 MICROPY_WASM_TRUST_CA=/path/to/root.crt.der
# C defaults (optional): ports/micropython/mpconfig_wasm.h
# Browser platform: ports/micropython/webassembly/ + WASMMOD_EMSCRIPTEN=1
#   Out-of-tree variant: make -C ports/micropython/webassembly  (VARIANT_DIR=…/variant)
# Port flash hook: #define MICROPY_WASM_TRUST_BOOT() … mp_wasm_trust_add(…)

WASMMOD_DIR ?= extmod/wasmmod
WAMR_DIR ?= $(WASMMOD_DIR)/third_party/wamr

# Host I/O: unix product-mini needs pthread/dl; browser (Emscripten) does not.
WASMMOD_EMSCRIPTEN ?= 0

MICROPY_PY_WASM_AOT ?= 0
MICROPY_PY_WASM_JIT ?= 0
MICROPY_PY_WASM_FAST_JIT ?= 0
MICROPY_PY_WASM_MATRIX ?= 0
MICROPY_WASM_VERIFY ?= 0

# Container engines are compile-time, not a runtime "browser mode" flag.
# WASMMOD_EMSCRIPTEN=1 (webassembly variant): no ET_REL loader linked, finder
# probes .wasm only, I/O via js.fetch. Unix/Metal: ELF + AOT + Wasm preference.
# See docs/PACK.md ("Browser: wasm-only") and ports/micropython/webassembly/README.md.
ifeq ($(WASMMOD_EMSCRIPTEN),1)
MICROPY_PY_WASM_ELF ?= 0
MICROPY_WASM_CONTAINERS ?= wasm
else
MICROPY_PY_WASM_ELF ?= 1
MICROPY_WASM_CONTAINERS ?= elf,aot,wasm
endif

SRC_WASMMOD = \
	$(WASMMOD_DIR)/wasmmod.c \
	$(WASMMOD_DIR)/modobj.c \
	$(WASMMOD_DIR)/modapi.c \
	$(WASMMOD_DIR)/packload.c \
	$(WASMMOD_DIR)/fetch.c \
	$(WASMMOD_DIR)/finder.c \
	$(WASMMOD_DIR)/forward.c \
	$(WASMMOD_DIR)/host.c \
	$(WASMMOD_DIR)/loader.c \
	$(WASMMOD_DIR)/pack.c \
	$(WASMMOD_DIR)/cdn.c \
	$(WASMMOD_DIR)/resolve.c \
	$(WASMMOD_DIR)/runtime.c \
	$(WASMMOD_DIR)/source.c \
	$(WASMMOD_DIR)/inspect.c \
	$(WASMMOD_DIR)/upy_catalog.c \
	$(WASMMOD_DIR)/verify.c \
	$(WASMMOD_DIR)/zlibutil.c \
	$(WASMMOD_DIR)/format/common/format.c \
	$(WASMMOD_DIR)/format/wasm/section.c \
	$(WASMMOD_DIR)/format/aot/section.c \
	$(WASMMOD_DIR)/format/elf/section.c

ifeq ($(MICROPY_PY_WASM_ELF),1)
SRC_WASMMOD += $(WASMMOD_DIR)/format/elf/load.c
endif

# Included after extmod.mk's PY_O += SRC_EXTMOD_C, so append objects here.
PY_O += $(addprefix $(BUILD)/, $(SRC_WASMMOD:.c=.o))
SRC_QSTR += $(SRC_WASMMOD)
QSTR_DEFS += $(TOP)/$(WASMMOD_DIR)/qstrdefs.wasmmod

# Nested WAMR: top-level `make submodules` is not recursive by default.
GIT_SUBMODULES += $(WASMMOD_DIR)
submodules: sync-wasmmod-nested
.PHONY: sync-wasmmod-nested
sync-wasmmod-nested:
	$(Q)cd $(TOP) && git submodule update --init --recursive $(WASMMOD_DIR)

# Config-specific WAMR tree so AOT/JIT flag flips do not require a manual wipe.
WAMR_BUILD ?= $(BUILD)/wamr-a$(MICROPY_PY_WASM_AOT)j$(MICROPY_PY_WASM_JIT)f$(MICROPY_PY_WASM_FAST_JIT)

# Package release string → wasm.version (single source: VERSION).
MICROPY_WASM_VERSION ?= $(shell tr -d '[:space:]' < $(TOP)/$(WASMMOD_DIR)/VERSION)
# WAMR AOT file-format version → wasm.AOT_VERSION / .aotN filenames.
MICROPY_WASM_AOT_VERSION ?= $(shell sed -n 's/^#define AOT_CURRENT_VERSION \([0-9][0-9]*\)/\1/p' $(TOP)/$(WAMR_DIR)/core/config.h)

INC += -I$(TOP)/$(WAMR_DIR)/core/iwasm/include
CFLAGS_EXTMOD += -DMICROPY_PY_WASM=1 \
	-DMICROPY_PY_WASM_AOT=$(MICROPY_PY_WASM_AOT) \
	-DMICROPY_PY_WASM_ELF=$(MICROPY_PY_WASM_ELF) \
	-DMICROPY_PY_WASM_JIT=$(MICROPY_PY_WASM_JIT) \
	-DMICROPY_PY_WASM_FAST_JIT=$(MICROPY_PY_WASM_FAST_JIT) \
	-DMICROPY_PY_WASM_MATRIX=$(MICROPY_PY_WASM_MATRIX) \
	-DMICROPY_WASM_VERIFY=$(MICROPY_WASM_VERIFY) \
	-DMICROPY_WASM_VERSION=\"$(MICROPY_WASM_VERSION)\" \
	-DMICROPY_WASM_AOT_VERSION=$(MICROPY_WASM_AOT_VERSION) \
	-DMICROPY_WASM_CONTAINERS=\"$(MICROPY_WASM_CONTAINERS)\"

ifeq ($(WASMMOD_EMSCRIPTEN),1)
LDFLAGS_EXTMOD += -L$(WAMR_BUILD) -liwasm -lm
# Browser platform: js.fetch I/O (ports/micropython/webassembly/).
SRC_WASMMOD_WEB = $(WASMMOD_DIR)/ports/micropython/webassembly/io_browser.c
PY_O += $(addprefix $(BUILD)/, $(SRC_WASMMOD_WEB:.c=.o))
SRC_QSTR += $(SRC_WASMMOD_WEB)
else
LDFLAGS_EXTMOD += -L$(WAMR_BUILD) -liwasm -lpthread -ldl -lm
endif
ifneq ($(filter 1,$(MICROPY_PY_WASM_JIT) $(MICROPY_PY_WASM_FAST_JIT)),)
LDFLAGS_EXTMOD += -lstdc++
endif

# Embed public root CA DER(s) → zlib ROM + lazy mp_wasm_trust_load_builtin().
# Multiple roots: MICROPY_WASM_TRUST_CA="a.der b.der"
ifneq ($(strip $(MICROPY_WASM_TRUST_CA)),)
WASM_TRUST_CA_C = $(BUILD)/wasm_trust_ca.c
WASM_TRUST_CA_O = $(BUILD)/wasm_trust_ca.o
PY_O += $(WASM_TRUST_CA_O)
CFLAGS_EXTMOD += -DMICROPY_WASM_TRUST_INFLATE=1

$(WASM_TRUST_CA_C): $(MICROPY_WASM_TRUST_CA) $(TOP)/$(WASMMOD_DIR)/tools/wasmmod_embed_ca.py
	$(Q)$(MKDIR) -p $(dir $@)
	$(Q)python3 $(TOP)/$(WASMMOD_DIR)/tools/wasmmod.py embed-ca -o $@ $(MICROPY_WASM_TRUST_CA)

$(WASM_TRUST_CA_O): $(WASM_TRUST_CA_C)
	$(Q)$(CC) $(CFLAGS) $(CFLAGS_EXTMOD) $(INC) -c -o $@ $<
endif

WAMR_CMAKE_EXTRA =
LLVM_CONFIG ?= $(shell command -v llvm-config-18 2>/dev/null || command -v llvm-config 2>/dev/null)
ifneq ($(MICROPY_PY_WASM_JIT),0)
LLVM_CMAKE_DIR ?= $(shell $(LLVM_CONFIG) --cmakedir 2>/dev/null)
ifneq ($(LLVM_CMAKE_DIR),)
WAMR_CMAKE_EXTRA += -DLLVM_DIR=$(LLVM_CMAKE_DIR)
endif
LDFLAGS_EXTMOD += $(shell $(LLVM_CONFIG) --ldflags --libs 2>/dev/null) \
	-Wl,-rpath,$(shell $(LLVM_CONFIG) --libdir 2>/dev/null)
endif

ifeq ($(WASMMOD_EMSCRIPTEN),1)
# Emscripten host: toolchain + C invokeNative (no host asm) + WASI type shim.
EMSCRIPTEN_CMAKE_TOOLCHAIN ?= $(shell dirname $$(command -v emcc 2>/dev/null))/cmake/Modules/Platform/Emscripten.cmake
WAMR_EM_WASI_SHIM ?= $(abspath $(TOP)/$(WASMMOD_DIR)/ports/micropython/webassembly/wamr_em_wasi_shim.h)
WAMR_CMAKE_EXTRA += \
	-DCMAKE_TOOLCHAIN_FILE=$(EMSCRIPTEN_CMAKE_TOOLCHAIN) \
	-DCMAKE_C_FLAGS="-include $(WAMR_EM_WASI_SHIM)" \
	-DWAMR_BUILD_TARGET=X86_32 \
	-DWAMR_BUILD_INVOKE_NATIVE_GENERAL=1
endif

$(WAMR_BUILD)/libiwasm.a:
	$(Q)$(MKDIR) -p $(WAMR_BUILD)
	$(Q)cmake -S $(TOP)/$(WAMR_DIR)/product-mini/platforms/linux -B $(WAMR_BUILD) \
		-DWAMR_BUILD_INTERP=1 \
		-DWAMR_BUILD_AOT=$(MICROPY_PY_WASM_AOT) \
		-DWAMR_BUILD_JIT=$(MICROPY_PY_WASM_JIT) \
		-DWAMR_BUILD_FAST_JIT=$(MICROPY_PY_WASM_FAST_JIT) \
		-DWAMR_BUILD_LIBC_BUILTIN=1 \
		-DWAMR_BUILD_LIBC_WASI=0 \
		-DWAMR_BUILD_SIMD=0 \
		-DWAMR_BUILD_MULTI_MODULE=0 \
		-DWAMR_DISABLE_HW_BOUND_CHECK=1 \
		-DWAMR_BUILD_LOAD_CUSTOM_SECTION=1 \
		$(WAMR_CMAKE_EXTRA)
	$(Q)+$(MAKE) -C $(WAMR_BUILD) vmlib

$(BUILD)/$(WASMMOD_DIR)/wasmmod.o $(BUILD)/$(WASMMOD_DIR)/modobj.o \
$(BUILD)/$(WASMMOD_DIR)/modapi.o $(BUILD)/$(WASMMOD_DIR)/packload.o \
$(BUILD)/$(WASMMOD_DIR)/runtime.o \
$(BUILD)/$(WASMMOD_DIR)/pack.o $(BUILD)/$(WASMMOD_DIR)/finder.o \
$(BUILD)/$(WASMMOD_DIR)/forward.o $(BUILD)/$(WASMMOD_DIR)/fetch.o \
$(BUILD)/$(WASMMOD_DIR)/cdn.o $(BUILD)/$(WASMMOD_DIR)/resolve.o \
$(BUILD)/$(WASMMOD_DIR)/verify.o $(BUILD)/$(WASMMOD_DIR)/host.o \
$(BUILD)/$(WASMMOD_DIR)/source.o $(BUILD)/$(WASMMOD_DIR)/zlibutil.o \
$(BUILD)/$(WASMMOD_DIR)/format/common/format.o \
$(BUILD)/$(WASMMOD_DIR)/format/wasm/section.o \
$(BUILD)/$(WASMMOD_DIR)/format/aot/section.o \
$(BUILD)/$(WASMMOD_DIR)/format/elf/section.o: $(WAMR_BUILD)/libiwasm.a

ifeq ($(MICROPY_PY_WASM_ELF),1)
$(BUILD)/$(WASMMOD_DIR)/format/elf/load.o: $(WAMR_BUILD)/libiwasm.a
endif

ifeq ($(WASMMOD_EMSCRIPTEN),1)
$(BUILD)/$(WASMMOD_DIR)/ports/micropython/webassembly/io_browser.o: $(WAMR_BUILD)/libiwasm.a
endif

ifeq ($(MICROPY_PY_WASM_MATRIX),1)
WASM_HOST_MATRIX_O = $(BUILD)/$(WASMMOD_DIR)/examples/host_matrix.o
$(WASM_HOST_MATRIX_O): $(TOP)/$(WASMMOD_DIR)/examples/host_matrix.rs
	$(Q)$(MKDIR) -p $(dir $@)
	$(Q)rustc -Copt-level=2 --crate-type=lib --emit=obj -Cpanic=abort -o $@ $<
PY_O += $(WASM_HOST_MATRIX_O)
endif
