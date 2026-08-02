# Build all wasmmod demos and run the explained call-matrix smoke.
#
#   make -C extmod/wasmmod/examples test
#   make -C examples/wasmmod test          # via metalpython symlink
#   make -C extmod/wasmmod/examples test-engines
#
# Engine rounds use separate ports/unix BUILD dirs so configs do not clobber.
# AOT needs a wamrc built from the same lib/wamr (AOT binary version must match).

include find_top.mk
UNIX = $(TOP)/ports/unix
# MATRIX=1 pulls host_matrix.rs + wasm.c_call / host_*_triple.
HOST_COMMON = MICROPY_PY_WASM=1 MICROPY_PY_WASM_MATRIX=1 MICROPY_PY_BTREE=0 MICROPY_PY_FFI=0

MPY_INTERP = $(UNIX)/build-wasm/micropython
MPY_AOT = $(UNIX)/build-wasm-aot/micropython
MPY_FJIT = $(UNIX)/build-wasm-fjit/micropython
MPY_JIT = $(UNIX)/build-wasm-jit/micropython

# Prefer wamrc from this tree's WAMR (version-matched). Override with WAMRC=...
WAMRC_BUILD ?= $(UNIX)/build-wamrc
WAMRC ?= $(WAMRC_BUILD)/wamrc
LLVM_CMAKE_DIR ?= $(shell llvm-config-18 --cmakedir 2>/dev/null || llvm-config --cmakedir 2>/dev/null)

PACK_WASMS = \
	hello/hello.wasm \
	client/client.wasm \
	mixed/mixed.wasm \
	bridge/bridge.wasm

.PHONY: all clean test test-aot test-fast-jit test-jit test-engines \
	packs aot-packs wamrc host host-aot host-fast-jit host-jit

packs:
	$(MAKE) -C hello
	$(MAKE) -C client
	$(MAKE) -C mixed
	$(MAKE) -C bridge

all: packs

# Build wamrc from lib/wamr so .aot matches the linked AOT loader.
wamrc:
	@if [ -z "$(LLVM_CMAKE_DIR)" ]; then \
		echo "FAIL: llvm-config not found (needed to build matching wamrc)"; \
		exit 1; \
	fi
	cmake -S $(TOP)/lib/wamr/wamr-compiler -B $(WAMRC_BUILD) \
		-DWAMR_BUILD_WITH_CUSTOM_LLVM=1 \
		-DLLVM_DIR=$(LLVM_CMAKE_DIR)
	cmake --build $(WAMRC_BUILD) -j$$(nproc)

aot-packs: packs wamrc
	@for w in $(PACK_WASMS); do \
		aot=$${w%.wasm}.aot; \
		echo "AOT $$w -> $$aot"; \
		$(WAMRC) -o $$aot $$w; \
	done

host:
	$(MAKE) -C $(UNIX) $(HOST_COMMON) BUILD=build-wasm

host-aot:
	$(MAKE) -C $(UNIX) $(HOST_COMMON) MICROPY_PY_WASM_AOT=1 BUILD=build-wasm-aot

host-fast-jit:
	$(MAKE) -C $(UNIX) $(HOST_COMMON) MICROPY_PY_WASM_FAST_JIT=1 BUILD=build-wasm-fjit

host-jit:
	$(MAKE) -C $(UNIX) $(HOST_COMMON) MICROPY_PY_WASM_JIT=1 BUILD=build-wasm-jit

# Default smoke: interpreter (no AOT/JIT).
test: packs host
	$(MPY_INTERP) $(CURDIR)/run_matrix.py

test-aot: aot-packs host-aot
	$(MPY_AOT) $(CURDIR)/run_matrix.py

test-fast-jit: packs host-fast-jit
	$(MPY_FJIT) $(CURDIR)/run_matrix.py

# Soft-skip when system LLVM cmake is incomplete (common on minimal llvm-*-dev).
test-jit: packs
	@if $(MAKE) host-jit; then \
		$(MPY_JIT) $(CURDIR)/run_matrix.py; \
	else \
		echo ""; \
		echo "SKIP test-jit: LLVM JIT host build failed."; \
		echo "      Needs llvm-config (+ libcurl/libedit/zstd cmake deps) and linkable -lLLVM."; \
		echo "      Fast JIT already covers in-process JIT without full LLVM."; \
	fi

# Full engine sweep: interp + AOT + Fast JIT; LLVM JIT if the host can link it.
test-engines: test test-aot test-fast-jit test-jit
	@echo ""
	@echo "ENGINE ROUNDS DONE (interp + AOT + Fast JIT; LLVM JIT if not skipped)"

clean:
	$(MAKE) -C hello clean
	$(MAKE) -C client clean
	$(MAKE) -C mixed clean
	$(MAKE) -C bridge clean
	rm -f hello/hello.aot client/client.aot mixed/mixed.aot bridge/bridge.aot
