# MicroPython / metalpython make fragment for wasmmod + nested WAMR.
# Included from extmod/extmod.mk when MICROPY_PY_WASM=1.

WASMMOD_DIR ?= extmod/wasmmod
WAMR_DIR ?= $(WASMMOD_DIR)/third_party/wamr

SRC_WASMMOD = \
	$(WASMMOD_DIR)/wasmmod.c \
	$(WASMMOD_DIR)/fetch.c \
	$(WASMMOD_DIR)/finder.c \
	$(WASMMOD_DIR)/forward.c \
	$(WASMMOD_DIR)/host.c \
	$(WASMMOD_DIR)/pack.c \
	$(WASMMOD_DIR)/runtime.c \
	$(WASMMOD_DIR)/verify.c

SRC_EXTMOD += $(SRC_WASMMOD)

# Nested WAMR: top-level `make submodules` is not recursive by default.
GIT_SUBMODULES += $(WASMMOD_DIR)
submodules: sync-wasmmod-nested
.PHONY: sync-wasmmod-nested
sync-wasmmod-nested:
	$(Q)cd $(TOP) && git submodule update --init --recursive $(WASMMOD_DIR)

MICROPY_PY_WASM_AOT ?= 0
MICROPY_PY_WASM_JIT ?= 0
MICROPY_PY_WASM_FAST_JIT ?= 0
MICROPY_PY_WASM_MATRIX ?= 0
MICROPY_WASM_VERIFY ?= 0

# Config-specific WAMR tree so AOT/JIT flag flips do not require a manual wipe.
WAMR_BUILD ?= $(BUILD)/wamr-a$(MICROPY_PY_WASM_AOT)j$(MICROPY_PY_WASM_JIT)f$(MICROPY_PY_WASM_FAST_JIT)

INC += -I$(TOP)/$(WAMR_DIR)/core/iwasm/include
CFLAGS_EXTMOD += -DMICROPY_PY_WASM=1 \
	-DMICROPY_PY_WASM_AOT=$(MICROPY_PY_WASM_AOT) \
	-DMICROPY_PY_WASM_JIT=$(MICROPY_PY_WASM_JIT) \
	-DMICROPY_PY_WASM_FAST_JIT=$(MICROPY_PY_WASM_FAST_JIT) \
	-DMICROPY_PY_WASM_MATRIX=$(MICROPY_PY_WASM_MATRIX) \
	-DMICROPY_WASM_VERIFY=$(MICROPY_WASM_VERIFY)
LDFLAGS_EXTMOD += -L$(WAMR_BUILD) -liwasm -lpthread -ldl -lm
ifneq ($(filter 1,$(MICROPY_PY_WASM_JIT) $(MICROPY_PY_WASM_FAST_JIT)),)
LDFLAGS_EXTMOD += -lstdc++
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
		$(WAMR_CMAKE_EXTRA)
	$(Q)cmake --build $(WAMR_BUILD) --target vmlib -j

$(BUILD)/$(WASMMOD_DIR)/wasmmod.o $(BUILD)/$(WASMMOD_DIR)/runtime.o \
$(BUILD)/$(WASMMOD_DIR)/pack.o $(BUILD)/$(WASMMOD_DIR)/finder.o \
$(BUILD)/$(WASMMOD_DIR)/forward.o $(BUILD)/$(WASMMOD_DIR)/fetch.o \
$(BUILD)/$(WASMMOD_DIR)/verify.o $(BUILD)/$(WASMMOD_DIR)/host.o: $(WAMR_BUILD)/libiwasm.a

ifeq ($(MICROPY_PY_WASM_MATRIX),1)
WASM_HOST_MATRIX_O = $(BUILD)/$(WASMMOD_DIR)/examples/host_matrix.o
$(WASM_HOST_MATRIX_O): $(TOP)/$(WASMMOD_DIR)/examples/host_matrix.rs
	$(Q)$(MKDIR) -p $(dir $@)
	$(Q)rustc -Copt-level=2 --crate-type=lib --emit=obj -Cpanic=abort -o $@ $<
PY_O += $(WASM_HOST_MATRIX_O)
endif
