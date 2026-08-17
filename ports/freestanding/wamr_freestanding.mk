# wasmmod OWN: freestanding WAMR (interp + shared heap).
# A host kernel with no OS under it supplies the platform GLUE and links the .a;
# it passes its own WAMR platform name and include dirs, so this file names no
# downstream tree. emcc (EMCC=1) needs none of that: wasmmod's own
# ports/webassembly/wamr is the platform there.
#
# Required:
#   WAMR_DIR         — path to WAMR tree (default: $(WASMMOD_DIR)/third_party/wamr)
#   OUT_DIR          — object/archive output directory
#   PLAT_BH_PLATFORM — WAMR platform selector, compiled as BH_PLATFORM_<this>
#   PLAT_INC         — platform_internal.h
#   PLAT_PORT_INC    — extra -I (the kernel's port root)
#   PLAT_LIBC_INC    — extra -I (freestanding libc headers)
#   PLAT_SRC_INC     — extra -I (card tree)
#   PLAT_EXTRA_INC   — extra -I
#
# Optional:
#   ARCH=x86_64|x86_32|armv7  — default x86_64
#   UEFI=0|1            — Windows-gnu vs none-elf target (default 0)
#   CC / AR
#   WASMMOD_DIR         — this wasmmod root (auto from this mk location)
#
# Output: $(OUT_DIR)/libwasmmod_wamr_freestanding.a
#
# Rewrite loader needs shared heap (unix cargo: WAMR_BUILD_SHARED_HEAP=1).
# AOT/fast-jit stay off here (no LLVM; fast-jit rejects shared heap).

WASMMOD_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
WAMR_DIR ?= $(WASMMOD_DIR)/third_party/wamr
ARCH ?= x86_64
UEFI ?= 0
# clang required for --target= (host CC=gcc breaks freestanding).
CC := clang
AR ?= ar

ifeq ($(strip $(OUT_DIR)),)
$(error OUT_DIR is required)
endif
ifeq ($(EMCC),1)
PLAT_BH_PLATFORM ?= WASMMOD
PLAT_INC ?= $(WASMMOD_DIR)/ports/webassembly/wamr
PLAT_PORT_INC ?= $(PLAT_INC)
PLAT_LIBC_INC ?= $(PLAT_INC)
PLAT_SRC_INC ?= $(WASMMOD_DIR)/src
PLAT_EXTRA_INC ?= $(WASMMOD_DIR)/src
endif
ifeq ($(strip $(PLAT_INC)),)
$(error PLAT_INC is required)
endif
ifeq ($(strip $(PLAT_BH_PLATFORM)),)
$(error PLAT_BH_PLATFORM is required)
endif
ifeq ($(wildcard $(WAMR_DIR)/core/iwasm/include/wasm_export.h),)
$(error WAMR missing at $(WAMR_DIR) — wasmmod OWN third_party/wamr only)
endif
ifeq ($(filter $(ARCH),x86_64 x86_32 armv7),)
$(error ARCH must be x86_64, x86_32 or armv7 (got $(ARCH)))
endif

CORE := $(WAMR_DIR)/core
IWASM := $(CORE)/iwasm
SHARED := $(CORE)/shared

LIB := $(OUT_DIR)/libwasmmod_wamr_freestanding.a
OBJDIR := $(OUT_DIR)/obj

SRCS := \
	$(SHARED)/mem-alloc/ems/ems_alloc.c \
	$(SHARED)/mem-alloc/ems/ems_gc.c \
	$(SHARED)/mem-alloc/ems/ems_hmu.c \
	$(SHARED)/mem-alloc/ems/ems_kfc.c \
	$(SHARED)/mem-alloc/mem_alloc.c \
	$(SHARED)/utils/bh_assert.c \
	$(SHARED)/utils/bh_bitmap.c \
	$(SHARED)/utils/bh_common.c \
	$(SHARED)/utils/bh_hashmap.c \
	$(SHARED)/utils/bh_leb128.c \
	$(SHARED)/utils/bh_list.c \
	$(SHARED)/utils/bh_log.c \
	$(SHARED)/utils/bh_queue.c \
	$(SHARED)/utils/bh_vector.c \
	$(SHARED)/utils/runtime_timer.c \
	$(IWASM)/common/wasm_blocking_op.c \
	$(IWASM)/common/wasm_c_api.c \
	$(IWASM)/common/wasm_exec_env.c \
	$(IWASM)/common/wasm_loader_common.c \
	$(IWASM)/common/wasm_memory.c \
	$(IWASM)/common/wasm_native.c \
	$(IWASM)/common/wasm_runtime_common.c \
	$(IWASM)/common/wasm_shared_memory.c \
	$(IWASM)/libraries/shared-heap/shared_heap_wrapper.c \
	$(IWASM)/interpreter/wasm_interp_fast.c \
	$(IWASM)/interpreter/wasm_loader.c \
	$(IWASM)/interpreter/wasm_runtime.c

ifeq ($(EMCC),1)
# Nested WAMR inside emcc wasm32. Interp only; general C tramp (no host asm).
SRCS += $(IWASM)/common/arch/invokeNative_general.c
BUILD_TARGET_FLAG := -DBUILD_TARGET_X86_32
CLANG_TARGET :=
ARCH_CFLAGS :=
UEFI_CFLAGS :=
CPPFLAGS_EXTRA :=
else
ifeq ($(ARCH),armv7)
# Cortex-A7 hard-float: WAMR's own VFP trampoline, no UEFI variant.
SRCS += $(IWASM)/common/arch/invokeNative_arm_vfp.s
BUILD_TARGET_FLAG := -DBUILD_TARGET_ARM_VFP
CLANG_TARGET := armv7-none-eabihf
ARCH_CFLAGS := -marm -mfpu=neon-vfpv4 -mfloat-abi=hard
UEFI_CFLAGS :=
CPPFLAGS_EXTRA :=
else
ARCH_CFLAGS :=
ifeq ($(ARCH),x86_32)
# No mingw-ia32 trampoline in-tree; general C tramp works for both BIOS/UEFI.
SRCS += $(IWASM)/common/arch/invokeNative_general.c
BUILD_TARGET_FLAG := -DBUILD_TARGET_X86_32
ifeq ($(UEFI),1)
CLANG_TARGET := i686-unknown-windows-gnu
UEFI_CFLAGS := -fshort-wchar -mno-mmx -mno-sse -mno-sse2
CPPFLAGS_EXTRA := -Dstrtok_s=strtok_r
else
CLANG_TARGET := i686-unknown-none-elf
UEFI_CFLAGS :=
endif
else
BUILD_TARGET_FLAG := -DBUILD_TARGET_X86_64
ifeq ($(UEFI),1)
# general.c passes each uint32 as its own arg — 64-bit exec_env splits.
# mingw_x64.s uses movsd; this seat is -mno-sse.
TRAMP_WIN64_NOSSE := $(WASMMOD_DIR)/ports/freestanding/invokeNative_win64_nosse.s
CLANG_TARGET := x86_64-unknown-windows-gnu
UEFI_CFLAGS := -fshort-wchar -mno-red-zone -mno-mmx -mno-sse -mno-sse2
CPPFLAGS_EXTRA := -Dstrtok_s=strtok_r
else
SRCS += $(IWASM)/common/arch/invokeNative_em64.s
CLANG_TARGET := x86_64-unknown-none-elf
UEFI_CFLAGS :=
CPPFLAGS_EXTRA :=
endif
endif
endif
endif

# Unique object names (WAMR-relative path with / → _).
OBJS := $(foreach s,$(SRCS),$(OBJDIR)/$(subst /,_,$(subst $(WAMR_DIR)/,,$(s))).o)
ifneq ($(TRAMP_WIN64_NOSSE),)
OBJS += $(OBJDIR)/invokeNative_win64_nosse.o
endif

CPPFLAGS := \
	-I$(PLAT_INC) \
	-I$(PLAT_PORT_INC) \
	-I$(PLAT_LIBC_INC) \
	-I$(PLAT_SRC_INC) \
	-I$(PLAT_EXTRA_INC) \
	-I$(IWASM)/include \
	-I$(IWASM)/interpreter \
	-I$(IWASM)/common \
	-I$(IWASM)/libraries/shared-heap \
	-I$(SHARED)/platform/include \
	-I$(SHARED)/mem-alloc \
	-I$(SHARED)/utils \
	-I$(SHARED)/utils/uncommon \
	-I$(CORE) \
	-DBH_PLATFORM_$(PLAT_BH_PLATFORM) \
	$(BUILD_TARGET_FLAG) \
	-DWASM_ENABLE_INTERP=1 \
	-DWASM_ENABLE_FAST_INTERP=1 \
	-DWASM_ENABLE_AOT=0 \
	-DWASM_ENABLE_JIT=0 \
	-DWASM_ENABLE_FAST_JIT=0 \
	-DWASM_ENABLE_LIBC_BUILTIN=0 \
	-DWASM_ENABLE_LIBC_WASI=0 \
	-DWASM_ENABLE_SIMD=0 \
	-DWASM_ENABLE_MULTI_MODULE=0 \
	-DWASM_ENABLE_SHARED_MEMORY=0 \
	-DWASM_ENABLE_SHARED_HEAP=1 \
	-DWASM_ENABLE_MINI_LOADER=0 \
	-DWASM_DISABLE_HW_BOUND_CHECK=1 \
	-DWASM_DISABLE_STACK_HW_BOUND_CHECK=1 \
	-DWASM_ENABLE_BULK_MEMORY=1 \
	-DBH_MALLOC=wasm_runtime_malloc \
	-DBH_FREE=wasm_runtime_free \
	$(CPPFLAGS_EXTRA)

ifeq ($(EMCC),1)
CC := emcc
AR := emar
CFLAGS := \
	-std=c11 -O2 \
	-fno-strict-aliasing \
	-fno-stack-protector \
	-Wno-unused-parameter \
	-Wno-sign-compare \
	-Wno-missing-field-initializers \
	-Wno-format \
	-Wno-unused-command-line-argument \
	-U__linux__ -Ulinux -U__gnu_linux__ \
	-include $(PLAT_INC)/emcc_skip_wamr_wasi.h
ASFLAGS :=
else
# clang required for --target= (host CC=gcc breaks freestanding).
CC := clang
CLANG_RESOURCE_INC := $(shell $(CC) -print-resource-dir)/include
CFLAGS := \
	-std=c11 -O2 \
	-fno-strict-aliasing \
	-fno-stack-protector \
	-ffreestanding \
	-nostdinc \
	-isystem $(CLANG_RESOURCE_INC) \
	-Wno-unused-parameter \
	-Wno-sign-compare \
	-Wno-missing-field-initializers \
	-Wno-format \
	-Wno-unused-command-line-argument \
	-U__linux__ -Ulinux -U__gnu_linux__ \
	--target=$(CLANG_TARGET) \
	$(ARCH_CFLAGS) \
	$(UEFI_CFLAGS)

# Asm trampolines: do not pass C -D/-I (clang spam: argument unused).
ASFLAGS := --target=$(CLANG_TARGET) $(ARCH_CFLAGS)
endif

.PHONY: all
all: $(LIB)

$(LIB): $(OBJS)
	@mkdir -p $(dir $@)
	$(AR) crs $@ $(OBJS)

define COMPILE_one
$(OBJDIR)/$(subst /,_,$(subst $(WAMR_DIR)/,,$(1))).o: $(1)
	@mkdir -p $$(dir $$@)
ifeq ($(suffix $(1)),.s)
	$$(CC) $$(ASFLAGS) -c $$< -o $$@
else
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) -c $$< -o $$@
endif
endef
$(foreach s,$(SRCS),$(eval $(call COMPILE_one,$(s))))
ifneq ($(TRAMP_WIN64_NOSSE),)
$(OBJDIR)/invokeNative_win64_nosse.o: $(TRAMP_WIN64_NOSSE)
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@
endif

.PHONY: clean
clean:
	rm -rf $(OBJDIR) $(LIB)
