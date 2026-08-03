# Build all wasmmod demos and run the explained call-matrix smoke.
#
#   make -C extmod/wasmmod/examples test
#   make -C examples/wasmmod test          # via metalpython symlink
#   make -C extmod/wasmmod/examples test-engines
#
# Engine rounds use separate ports/unix BUILD dirs so configs do not clobber.
# AOT needs a wamrc built from the same nested WAMR (AOT binary version must match).

include find_top.mk
# test-engines uses bash arrays / PIPESTATUS for live tee + exit status.
SHELL := /bin/bash
UNIX = $(TOP)/ports/unix
WAMR_DIR = $(WASMMOD_ROOT)/third_party/wamr
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
	packs/hello.wasm \
	packs/client.wasm \
	packs/mixed.wasm \
	packs/bridge.wasm

.PHONY: all clean test test-aot test-fast-jit test-jit test-engines test-http demo repl \
	packs aot-packs wamrc host host-aot host-fast-jit host-jit

packs:
	$(MAKE) -C hello
	$(MAKE) -C client
	$(MAKE) -C mixed
	$(MAKE) -C bridge

all: packs

# Build wamrc from nested WAMR so .aot matches the linked AOT loader.
wamrc:
	@if [ -z "$(LLVM_CMAKE_DIR)" ]; then \
		echo "FAIL: llvm-config not found (needed to build matching wamrc)"; \
		exit 1; \
	fi
	@if [ ! -f "$(WAMR_DIR)/wamr-compiler/CMakeLists.txt" ]; then \
		echo "FAIL: WAMR missing at $(WAMR_DIR) — git submodule update --init --recursive"; \
		exit 1; \
	fi
	cmake -S $(WAMR_DIR)/wamr-compiler -B $(WAMRC_BUILD) \
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

# Real unix MicroPython REPL, stdin = demo_readme.py (not a faked >>> printer).
demo: packs host
	@cd $(CURDIR) && $(MPY_INTERP) -i < $(CURDIR)/demo_readme.py

# Interactive unix MicroPython in this directory (type it yourself).
# Hint: help('modules')  →  import wasm  →  wasm.load_pack(...)
repl: packs host
	@echo "MicroPython REPL — cwd=$(CURDIR)"
	@echo "Try:  help('modules')   import wasm   wasm.install_hook()"
	@cd $(CURDIR) && $(MPY_INTERP) -i

# Default smoke: interpreter (no AOT/JIT).
test: packs host
	$(MPY_INTERP) $(CURDIR)/run_matrix.py

# Static HTTP pack root (native C fetch). Serves packs/ as document root.
test-http: packs host
	@set -e; \
	portfile=$$(mktemp); \
	python3 $(CURDIR)/tools/pack_http_server.py --dir $(CURDIR)/packs --port 0 --write-port $$portfile & \
	spid=$$!; \
	trap 'kill $$spid 2>/dev/null || true; rm -f $$portfile' EXIT; \
	for i in 1 2 3 4 5 6 7 8 9 10; do \
		[ -s $$portfile ] && break; \
		sleep 0.1; \
	done; \
	port=$$(cat $$portfile); \
	echo "HTTP pack root http://127.0.0.1:$$port/"; \
	HTTP_PACK_ROOT=http://127.0.0.1:$$port/ $(MPY_INTERP) $(CURDIR)/run_http.py

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

# Full engine sweep with a final summary table (continues after a failing round).
ENGINE_SUMMARY ?= $(CURDIR)/.engine-summary
test-engines: packs
	@rm -f $(ENGINE_SUMMARY); \
	ec=0; \
	record() { printf '%s|%s|%s\n' "$$1" "$$2" "$$3" >> $(ENGINE_SUMMARY); }; \
	banner() { \
		echo ""; \
		echo "################################################################"; \
		echo "# Engine: $$1"; \
		echo "################################################################"; \
	}; \
	run_matrix() { \
		name="$$1"; mpy="$$2"; \
		if [ ! -x "$$mpy" ]; then record "$$name" SKIP "no binary"; return 0; fi; \
		out=$$(mktemp); \
		set +e; \
		"$$mpy" $(CURDIR)/run_matrix.py 2>&1 | tee "$$out"; \
		st=$${PIPESTATUS[0]}; \
		set -e; \
		if [ "$$st" -eq 0 ]; then \
			n=$$(sed -n 's/^\([0-9][0-9]*\) cases OK$$/\1/p' "$$out" | tail -1); \
			[ -n "$$n" ] || n="?"; \
			record "$$name" PASS "$$n"; \
		else \
			record "$$name" FAIL ""; \
			ec=1; \
		fi; \
		rm -f "$$out"; \
	}; \
	banner "Interp"; \
	if $(MAKE) host; then run_matrix "Interp" "$(MPY_INTERP)"; \
	else record "Interp" FAIL "host build"; ec=1; fi; \
	banner "AOT"; \
	if $(MAKE) aot-packs host-aot; then run_matrix "AOT" "$(MPY_AOT)"; \
	else record "AOT" FAIL "build"; ec=1; fi; \
	banner "Fast JIT"; \
	if $(MAKE) host-fast-jit; then run_matrix "Fast JIT" "$(MPY_FJIT)"; \
	else record "Fast JIT" FAIL "host build"; ec=1; fi; \
	banner "LLVM JIT"; \
	if $(MAKE) host-jit; then \
		run_matrix "LLVM JIT" "$(MPY_JIT)"; \
	else \
		echo ""; \
		echo "SKIP test-jit: LLVM JIT host build failed."; \
		echo "      Needs llvm-config (+ libcurl/libedit/zstd cmake deps) and linkable -lLLVM."; \
		echo "      Fast JIT already covers in-process JIT without full LLVM."; \
		record "LLVM JIT" SKIP "host build"; \
	fi; \
	echo ""; \
	echo "================================================================"; \
	echo "Engine summary"; \
	echo "================================================================"; \
	printf "%-12s %s\n" "Engine" "Result"; \
	echo "------------ ----------------"; \
	while IFS='|' read -r name st detail; do \
		case "$$st" in \
			PASS) printf "%-12s PASS (%s cases OK)\n" "$$name" "$$detail" ;; \
			FAIL) if [ -n "$$detail" ]; then printf "%-12s FAIL (%s)\n" "$$name" "$$detail"; \
			      else printf "%-12s FAIL\n" "$$name"; fi ;; \
			SKIP) printf "%-12s SKIP (%s)\n" "$$name" "$$detail" ;; \
			*)    printf "%-12s %s %s\n" "$$name" "$$st" "$$detail" ;; \
		esac; \
	done < $(ENGINE_SUMMARY); \
	echo "----------------------------------------------------------------"; \
	pass=$$(grep -c '|PASS|' $(ENGINE_SUMMARY) || true); \
	fail=$$(grep -c '|FAIL|' $(ENGINE_SUMMARY) || true); \
	skip=$$(grep -c '|SKIP|' $(ENGINE_SUMMARY) || true); \
	echo "$$pass passed, $$fail failed, $$skip skipped"; \
	if [ "$$fail" -eq 0 ]; then echo "ALL ENGINES OK"; else echo "ENGINE FAILURES"; fi; \
	rm -f $(ENGINE_SUMMARY); \
	exit $$ec

clean:
	$(MAKE) -C hello clean
	$(MAKE) -C client clean
	$(MAKE) -C mixed clean
	$(MAKE) -C bridge clean
	rm -f packs/*.aot
