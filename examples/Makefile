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
MPY_VERIFY = $(UNIX)/build-wasm-verify/micropython

# Dev PKI: trust/ = host CA; sign/ = pack leaf. Generated once; .keys/ is gitignored.
SIGN_KEY_DIR = $(CURDIR)/.keys
SIGN_ROOT = $(SIGN_KEY_DIR)/trust/root.crt.der
SIGN_LEAF_KEY = $(SIGN_KEY_DIR)/sign/leaf.key.pem
SIGN_CHAIN = $(SIGN_KEY_DIR)/sign/chain.der

PACK_WASMS = \
	packs/hello.wasm \
	packs/client.wasm \
	packs/mixed.wasm \
	packs/bridge.wasm \
	packs/test_a.wasm \
	packs/test_a.test_d.wasm \
	packs/test_a.test_b.test_c.wasm \
	packs/test_a2.wasm \
	packs/test_a2.test_d2.wasm \
	packs/test_a2.test_b2.test_c2.wasm

TREE_WASMS = \
	packs/test_a.wasm \
	packs/test_a.test_d.wasm \
	packs/test_a.test_b.test_c.wasm \
	packs/test_a2.wasm \
	packs/test_a2.test_d2.wasm \
	packs/test_a2.test_b2.test_c2.wasm

# Prefer wamrc from this tree's WAMR (version-matched). Override with WAMRC=...
WAMRC_BUILD ?= $(UNIX)/build-wamrc
WAMRC ?= $(WAMRC_BUILD)/wamrc
# AOT file-format tag (WAMR AOT_CURRENT_VERSION) → hello.aot6
AOT_VER ?= $(shell sed -n 's/^#define AOT_CURRENT_VERSION \([0-9][0-9]*\)/\1/p' $(WAMR_DIR)/core/config.h)
AOT_EXT := .aot$(AOT_VER)
LLVM_CMAKE_DIR ?= $(shell llvm-config-18 --cmakedir 2>/dev/null || llvm-config --cmakedir 2>/dev/null)

.PHONY: all clean test test-aot test-fast-jit test-jit test-engines test-http test-http-verify \
	test-verify test-signed test-tree test-tree-cdn test-tree-cdn-versions packs tree-packs aot-packs sign-packs sign-key wamrc \
	host host-aot host-fast-jit host-jit host-verify demo repl

packs:
	$(MAKE) -C hello
	$(MAKE) -C hello_elf
	$(MAKE) -C client
	$(MAKE) -C client_elf
	$(MAKE) -C host_elf
	$(MAKE) -C mixed
	$(MAKE) -C bridge
	$(MAKE) -C tree

tree-packs:
	$(MAKE) -C tree

all: packs

sign-key: $(SIGN_ROOT)

$(SIGN_ROOT) $(SIGN_LEAF_KEY) $(SIGN_CHAIN):
	mkdir -p $(SIGN_KEY_DIR)
	python3 $(WASM_SIGN) gen-pki --sub-ca -o $(SIGN_KEY_DIR)

# Detached .sig + .crt (leaf||sub) next to each pack. Device trusts SIGN_ROOT only.
sign-packs: packs sign-key
	@for w in $(PACK_WASMS); do \
		echo "SIGN $$w"; \
		python3 $(WASM_SIGN) sign --key $(SIGN_LEAF_KEY) --chain $(SIGN_CHAIN) $$w; \
		aot=$${w%.wasm}$(AOT_EXT); \
		if [ -f "$$aot" ]; then \
			echo "SIGN $$aot"; \
			python3 $(WASM_SIGN) sign --key $(SIGN_LEAF_KEY) --chain $(SIGN_CHAIN) $$aot; \
		fi; \
		aot_legacy=$${w%.wasm}.aot; \
		if [ "$$aot" != "$$aot_legacy" ] && [ -f "$$aot_legacy" ]; then \
			echo "SIGN $$aot_legacy"; \
			python3 $(WASM_SIGN) sign --key $(SIGN_LEAF_KEY) --chain $(SIGN_CHAIN) $$aot_legacy; \
		fi; \
	done
	@if [ -f packs/hello.elf ]; then \
		echo "SIGN packs/hello.elf"; \
		python3 $(WASM_SIGN) sign --key $(SIGN_LEAF_KEY) --chain $(SIGN_CHAIN) packs/hello.elf; \
		cp -f packs/hello.elf packs/hello.$$(uname -m).elf; \
	fi

# Build wamrc from nested WAMR so .aotN matches the linked AOT loader.
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
		aot=$${w%.wasm}$(AOT_EXT); \
		echo "AOT $$w -> $$aot"; \
		$(WAMRC) --emit-custom-sections=wasmmod.pack,wasmmod.imports,wasmmod.source -o $$aot $$w; \
	done
	$(MAKE) sign-packs

host:
	$(call clean-unix-build-qstr,build-wasm)
	$(MAKE) -C $(UNIX) $(HOST_COMMON) BUILD=build-wasm

host-verify: $(SIGN_ROOT)
	$(MAKE) -C $(UNIX) $(HOST_COMMON) MICROPY_WASM_VERIFY=1 \
		MICROPY_WASM_TRUST_CA=$(SIGN_ROOT) BUILD=build-wasm-verify

# Drop stale genhdr/frozen when engine flags change — otherwise unix can fail with
# "redeclaration of enumerator MP_QSTR_*" in frozen_content.c vs qstrdefs.generated.h.
define clean-unix-build-qstr
	rm -rf $(UNIX)/$(1)/genhdr \
		$(UNIX)/$(1)/frozen_content.c \
		$(UNIX)/$(1)/frozen_content.o
endef

host-aot:
	$(call clean-unix-build-qstr,build-wasm-aot)
	$(MAKE) -C $(UNIX) $(HOST_COMMON) MICROPY_PY_WASM_AOT=1 BUILD=build-wasm-aot

host-fast-jit:
	$(call clean-unix-build-qstr,build-wasm-fjit)
	$(MAKE) -C $(UNIX) $(HOST_COMMON) MICROPY_PY_WASM_FAST_JIT=1 BUILD=build-wasm-fjit

host-jit:
	$(call clean-unix-build-qstr,build-wasm-jit)
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

# Dotted module tree: sibling flat packs + nested monorepo (pack-tree).
test-tree: tree-packs host
	$(MPY_INTERP) $(CURDIR)/run_tree_cdn.py --suite=both

# Same as test-tree after publishing tree packs to a local metal-cdn.
# Default URL http://127.0.0.1:8000/cdn. Token optional when CDN allow-open publish.
test-tree-cdn: tree-packs host
	@set -e; \
	cdn_url="$${METAL_CDN_URL:-http://127.0.0.1:8000/cdn}"; \
	token="$${METAL_CDN_TOKEN:-}"; \
	PACKS="$(CURDIR)/packs"; \
	pub() { \
	  name="$$1"; file="$$2"; ver="$$3"; deps="$$4"; force="$${5:-false}"; \
	  echo "PUBLISH $$name@$$ver → $$cdn_url (force=$$force)"; \
	  auth=(); \
	  if [ -n "$$token" ]; then auth=(-H "Authorization: Bearer $$token"); fi; \
	  code=$$(curl -sS -o /tmp/wasmmod-tree-pub.json -w '%{http_code}' -X POST "$$cdn_url/publish" \
	    "$${auth[@]}" \
	    -F "meta={\"package\":\"$$name\",\"version\":\"$$ver\",\"lead\":true,\"pin\":true,\"force\":$$force,\"deps\":$$deps}" \
	    -F "files=@$$file;type=application/octet-stream"); \
	  echo "HTTP $$code"; \
	  test "$$code" = "201" -o "$$code" = "200"; \
	}; \
	pub test_a "$$PACKS/test_a.wasm" 0.1.0 '{}' true; \
	pub test_a.test_d "$$PACKS/test_a.test_d.wasm" 0.1.0 '{}' true; \
	pub test_a.test_b.test_c "$$PACKS/test_a.test_b.test_c.wasm" 0.1.0 '{"test_a.test_d":"0.1.0"}' true; \
	pub test_a2 "$$PACKS/test_a2.wasm" 0.1.0 '{}' true; \
	pub test_a2.test_d2 "$$PACKS/test_a2.test_d2.wasm" 0.1.0 '{}' true; \
	pub test_a2.test_b2.test_c2 "$$PACKS/test_a2.test_b2.test_c2.wasm" 0.1.0 '{"test_a2.test_d2":"0.1.0"}' true; \
	if [ -n "$$token" ]; then \
	  $(MPY_INTERP) $(CURDIR)/run_tree_cdn.py --suite=both --cdn --cdn-url=$$cdn_url --token=$$token; \
	else \
	  $(MPY_INTERP) $(CURDIR)/run_tree_cdn.py --suite=both --cdn --cdn-url=$$cdn_url; \
	fi

# Publish 0.1.0 → import → bump lead to 0.2.0 → import again; assert both pins remain.
test-tree-cdn-versions: tree-packs host
	@set -e; \
	cdn_url="$${METAL_CDN_URL:-http://127.0.0.1:8000/cdn}"; \
	token="$${METAL_CDN_TOKEN:-}"; \
	PACKS="$(CURDIR)/packs"; \
	auth=(); \
	if [ -n "$$token" ]; then auth=(-H "Authorization: Bearer $$token"); fi; \
	pub() { \
	  name="$$1"; file="$$2"; ver="$$3"; deps="$$4"; force="$${5:-false}"; \
	  echo "PUBLISH $$name@$$ver → $$cdn_url (force=$$force)"; \
	  code=$$(curl -sS -o /tmp/wasmmod-tree-pub.json -w '%{http_code}' -X POST "$$cdn_url/publish" \
	    "$${auth[@]}" \
	    -F "meta={\"package\":\"$$name\",\"version\":\"$$ver\",\"lead\":true,\"pin\":true,\"force\":$$force,\"deps\":$$deps}" \
	    -F "files=@$$file;type=application/octet-stream"); \
	  echo "HTTP $$code"; \
	  test "$$code" = "201" -o "$$code" = "200"; \
	}; \
	have() { \
	  stem="$$1"; ver="$$2"; \
	  for ext in .wasm.zlib .wasm; do \
	    code=$$(curl -sS -o /dev/null -w '%{http_code}' "$$cdn_url/artifacts/pin/$$ver/$$stem$$ext"); \
	    if [ "$$code" = "200" ]; then echo "OK pin $$stem@$$ver$$ext"; return 0; fi; \
	  done; \
	  echo "FAIL: missing pin $$stem@$$ver"; return 1; \
	}; \
	echo "=== publish 0.1.0 ==="; \
	pub test_a "$$PACKS/test_a.wasm" 0.1.0 '{}' true; \
	pub test_a.test_d "$$PACKS/test_a.test_d.wasm" 0.1.0 '{}' true; \
	pub test_a.test_b.test_c "$$PACKS/test_a.test_b.test_c.wasm" 0.1.0 '{"test_a.test_d":"0.1.0"}' true; \
	pub test_a2 "$$PACKS/test_a2.wasm" 0.1.0 '{}' true; \
	pub test_a2.test_d2 "$$PACKS/test_a2.test_d2.wasm" 0.1.0 '{}' true; \
	pub test_a2.test_b2.test_c2 "$$PACKS/test_a2.test_b2.test_c2.wasm" 0.1.0 '{"test_a2.test_d2":"0.1.0"}' true; \
	echo "=== import lead @0.1.0 ==="; \
	if [ -n "$$token" ]; then \
	  $(MPY_INTERP) $(CURDIR)/run_tree_cdn.py --suite=both --cdn --cdn-url=$$cdn_url --token=$$token; \
	else \
	  $(MPY_INTERP) $(CURDIR)/run_tree_cdn.py --suite=both --cdn --cdn-url=$$cdn_url; \
	fi; \
	echo "=== publish 0.2.0 (new pins, promote lead) ==="; \
	pub test_a "$$PACKS/test_a.wasm" 0.2.0 '{}' true; \
	pub test_a.test_d "$$PACKS/test_a.test_d.wasm" 0.2.0 '{}' true; \
	pub test_a.test_b.test_c "$$PACKS/test_a.test_b.test_c.wasm" 0.2.0 '{"test_a.test_d":"0.2.0"}' true; \
	pub test_a2 "$$PACKS/test_a2.wasm" 0.2.0 '{}' true; \
	pub test_a2.test_d2 "$$PACKS/test_a2.test_d2.wasm" 0.2.0 '{}' true; \
	pub test_a2.test_b2.test_c2 "$$PACKS/test_a2.test_b2.test_c2.wasm" 0.2.0 '{"test_a2.test_d2":"0.2.0"}' true; \
	echo "=== assert pins ==="; \
	have test_a 0.1.0; have test_a 0.2.0; \
	have test_a.test_d 0.1.0; have test_a.test_d 0.2.0; \
	have test_a2 0.1.0; have test_a2 0.2.0; \
	echo "=== import lead @0.2.0 ==="; \
	if [ -n "$$token" ]; then \
	  $(MPY_INTERP) $(CURDIR)/run_tree_cdn.py --suite=both --cdn --cdn-url=$$cdn_url --token=$$token; \
	else \
	  $(MPY_INTERP) $(CURDIR)/run_tree_cdn.py --suite=both --cdn --cdn-url=$$cdn_url; \
	fi; \
	echo "=== history page has both versions ==="; \
	html=$$(curl -sS "$$cdn_url/channels/lead/packs/test_a"); \
	echo "$$html" | grep -q 'id="history"'; \
	echo "$$html" | grep -q '0.1.0'; \
	echo "$$html" | grep -q '0.2.0'; \
	echo "OK version bump e2e"

# Require ECDSA signatures (detached .sig+.crt) + baked root CA in the host.
test-verify: sign-packs host-verify
	@test -f $(SIGN_ROOT) || (echo "FAIL: missing $(SIGN_ROOT)"; exit 1)
	$(MPY_VERIFY) $(CURDIR)/run_verify.py

# Static HTTP pack root (native C fetch). Serves packs/ as document root.
test-http: packs host
	@set -e; \
	portfile=$$(mktemp); \
	python3 -u $(WASMMOD_ROOT)/tools/wasmmod.py httpd --dir $(CURDIR)/packs --port 0 --write-port $$portfile & \
	spid=$$!; \
	trap 'kill $$spid 2>/dev/null || true; rm -f $$portfile' EXIT; \
	for i in 1 2 3 4 5 6 7 8 9 10; do \
		[ -s $$portfile ] && break; \
		sleep 0.1; \
	done; \
	port=$$(cat $$portfile); \
	echo "HTTP pack root http://127.0.0.1:$$port/"; \
	HTTP_PACK_ROOT=http://127.0.0.1:$$port/ $(MPY_INTERP) $(CURDIR)/run_http.py

# HTTP + signatures (same static tree; .sig/.crt fetched like VFS).
test-http-verify: sign-packs host-verify
	@set -e; \
	portfile=$$(mktemp); \
	python3 -u $(WASMMOD_ROOT)/tools/wasmmod.py httpd --dir $(CURDIR)/packs --port 0 --write-port $$portfile & \
	spid=$$!; \
	trap 'kill $$spid 2>/dev/null || true; rm -f $$portfile' EXIT; \
	for i in 1 2 3 4 5 6 7 8 9 10; do \
		[ -s $$portfile ] && break; \
		sleep 0.1; \
	done; \
	port=$$(cat $$portfile); \
	echo "HTTP signed pack root http://127.0.0.1:$$port/"; \
	HTTP_PACK_ROOT=http://127.0.0.1:$$port/ $(MPY_VERIFY) $(CURDIR)/run_http_verify.py

# sign-packs + VFS verify + HTTP verify (one shot).
test-signed: test-verify test-http-verify

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
	rm -f packs/*.aot packs/*.aot[0-9] packs/*.aot[0-9][0-9] packs/*.sig packs/*.crt packs/*.zlib
