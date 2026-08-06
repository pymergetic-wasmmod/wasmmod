# wasmmod OWN: freestanding WAMR (interp) for Metal.
# Metal supplies platform GLUE includes + links the resulting .a.
#
# Required:
#   WAMR_DIR          — path to WAMR tree (default: $(WASMMOD_DIR)/third_party/wamr)
#   OUT_DIR           — object/archive output directory
#   METAL_PLAT_INC    — Metal wasm/port/platform (platform_internal.h)
#   METAL_PORT_INC    — Metal wasm/port
#   METAL_LIBC_INC    — Metal freestanding libc headers
#   METAL_SRC_INC     — packages/metal/src
#   METAL_INCLUDE_INC — packages/metal/include
#
# Optional:
#   UEFI=0|1          — Windows-gnu vs none-elf target (default 0)
#   CC / AR
#   WASMMOD_DIR       — this wasmmod root (auto from this mk location)
#
# Output: $(OUT_DIR)/libwasmmod_wamr_freestanding.a

WASMMOD_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
WAMR_DIR ?= $(WASMMOD_DIR)/third_party/wamr
UEFI ?= 0
# clang required for --target= (host CC=gcc breaks freestanding).
CC := clang
AR ?= ar

ifeq ($(strip $(OUT_DIR)),)
$(error OUT_DIR is required)
endif
ifeq ($(strip $(METAL_PLAT_INC)),)
$(error METAL_PLAT_INC is required)
endif
ifeq ($(wildcard $(WAMR_DIR)/core/iwasm/include/wasm_export.h),)
$(error WAMR missing at $(WAMR_DIR) — wasmmod OWN third_party/wamr only)
endif

CORE := $(WAMR_DIR)/core
IWASM := $(CORE)/iwasm
SHARED := $(CORE)/shared

LIB := $(OUT_DIR)/libwasmmod_wamr_freestanding.a
OBJDIR := $(OUT_DIR)/obj

SRCS := \
	$(SHARED)/platform/common/math/math.c \
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
	$(IWASM)/interpreter/wasm_interp_fast.c \
	$(IWASM)/interpreter/wasm_loader.c \
	$(IWASM)/interpreter/wasm_runtime.c

ifeq ($(UEFI),1)
SRCS += $(IWASM)/common/arch/invokeNative_mingw_x64.s
else
SRCS += $(IWASM)/common/arch/invokeNative_em64.s
endif

# Unique object names (WAMR-relative path with / → _).
OBJS := $(foreach s,$(SRCS),$(OBJDIR)/$(subst /,_,$(subst $(WAMR_DIR)/,,$(s))).o)

CPPFLAGS := \
	-I$(METAL_PLAT_INC) \
	-I$(METAL_PORT_INC) \
	-I$(METAL_LIBC_INC) \
	-I$(METAL_SRC_INC) \
	-I$(METAL_INCLUDE_INC) \
	-I$(IWASM)/include \
	-I$(IWASM)/interpreter \
	-I$(IWASM)/common \
	-I$(SHARED)/platform/include \
	-I$(SHARED)/mem-alloc \
	-I$(SHARED)/utils \
	-I$(SHARED)/utils/uncommon \
	-I$(CORE) \
	-DBH_PLATFORM_METAL \
	-DBUILD_TARGET_X86_64 \
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
	-DWASM_ENABLE_MINI_LOADER=0 \
	-DWASM_DISABLE_HW_BOUND_CHECK=1 \
	-DWASM_DISABLE_STACK_HW_BOUND_CHECK=1 \
	-DWASM_ENABLE_BULK_MEMORY=1 \
	-DBH_MALLOC=wasm_runtime_malloc \
	-DBH_FREE=wasm_runtime_free

CFLAGS := \
	-std=c11 -O2 \
	-fno-strict-aliasing \
	-fno-stack-protector \
	-ffreestanding \
	-nostdinc \
	-Wno-unused-parameter \
	-Wno-sign-compare \
	-Wno-missing-field-initializers \
	-Wno-format \
	-U__linux__ -Ulinux -U__gnu_linux__

ifeq ($(UEFI),1)
CFLAGS += \
	--target=x86_64-unknown-windows-gnu \
	-fshort-wchar \
	-mno-red-zone \
	-mno-mmx -mno-sse -mno-sse2
CPPFLAGS += -DPM_METAL_WASM_TRAMP_WIN64
else
CFLAGS += --target=x86_64-unknown-none-elf
endif

.PHONY: all
all: $(LIB)

$(LIB): $(OBJS)
	@mkdir -p $(dir $@)
	$(AR) crs $@ $(OBJS)

define COMPILE_one
$(OBJDIR)/$(subst /,_,$(subst $(WAMR_DIR)/,,$(1))).o: $(1)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) -c $$< -o $$@
endef
$(foreach s,$(SRCS),$(eval $(call COMPILE_one,$(s))))

.PHONY: clean
clean:
	rm -rf $(OBJDIR) $(LIB)
