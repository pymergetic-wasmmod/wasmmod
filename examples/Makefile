# Every example pack, from source to signed artifact.
#
#   make            build packs/ from examples/*/
#   make sign-packs build, sign with .keys/sign, wrap the zlib twins
#   make clean      drop the artifacts (sources stay)
#
# packs/ used to hold artifacts whose sources had been lost, so a fresh clone
# could not produce them and the seed step signed whatever happened to be on
# disk. The sources here were recovered from the artifacts' own wasmmod.source
# section; each pack lists its exports/imports/deps in its pack.toml.
#
# Manifests are flat pack.toml, not card-tree __pmm__.toml, for every pack but
# hello: the pmm packer collects __impl__.c only, so a card-tree root cannot
# carry the Rust half these fixtures exist to exercise (docs/SOURCETREE.md,
# "examples/bridge/" — blocked on tooling, not a decision).

TOOLS_SRC := ../dev/tools/src
WASMMOD   := PYTHONPATH=$(TOOLS_SRC) python3 -m pymergetic.wasmmod.tools
PACKS     := packs
EX        := pymergetic.wasmmod_examples
KEY       := .keys/sign/leaf.key.pem
CHAIN     := .keys/sign/chain.der

# Frozen bytecode targets in some manifests need mpy-cross from the µPy tree
# this wasmmod sits in (../../.. == the checkout root).
MPY_CROSS ?= ../../../mpy-cross/build/mpy-cross

CC        ?= cc
CLANG     ?= clang
HOST_ARCH := $(shell uname -m)
AARCH64   ?= aarch64-linux-gnu

# Wasm packs: <dir> -> $(EX).<pack>.wasm
WASM_PACKS := \
  hello:hello \
  client:client \
  ticks:ticks \
  mixed:mixed \
  bridge:bridge \
  test_a:test_a \
  test_a_test_d:test_a.test_d \
  test_a_test_b_test_c:test_a.test_b.test_c \
  test_a2:test_a2 \
  test_a2/test_d2:test_a2.test_d2 \
  test_a2/test_b2/test_c2:test_a2.test_b2.test_c2

WASM_OUT := $(foreach p,$(WASM_PACKS),$(PACKS)/$(EX).$(word 2,$(subst :, ,$(p))).wasm)

# ELF packs: <dir> -> <pack>. Each builds a host-arch .elf plus the explicit
# arch names the CDN serves; the loader picks by arch, so an unnamed .elf is
# only useful next to them, never instead of them.
ELF_PACKS := hello_elf:hello client_elf:client hostcall:hostcall ticks_elf:ticks
ELF_ARCHES := x86_64 aarch64

ELF_OUT := \
  $(PACKS)/$(EX).hello.elf $(PACKS)/$(EX).hello.x86_64.elf $(PACKS)/$(EX).hello.aarch64.elf \
  $(PACKS)/$(EX).client.elf $(PACKS)/$(EX).client.x86_64.elf \
  $(PACKS)/$(EX).hostcall.elf $(PACKS)/$(EX).hostcall.x86_64.elf \
  $(PACKS)/$(EX).ticks.elf $(PACKS)/$(EX).ticks.x86_64.elf

# MPZL whole-artifact wrap, for the loader's compressed path.
ZLIB_OUT := \
  $(PACKS)/$(EX).hello.elf.zlib \
  $(PACKS)/$(EX).hello.x86_64.elf.zlib \
  $(PACKS)/$(EX).ticks.elf.zlib \
  $(PACKS)/$(EX).ticks.x86_64.elf.zlib

.PHONY: all sign-packs clean list
.SECONDARY:

all: $(WASM_OUT) $(ELF_OUT)

list:
	@echo "wasm: $(notdir $(WASM_OUT))"
	@echo "elf:  $(notdir $(ELF_OUT))"

$(PACKS):
	mkdir -p $(PACKS)

# --- wasm ------------------------------------------------------------------
# One rule per pack: the recipe is identical, only dir and pack name differ.
define WASM_RULE
$(PACKS)/$(EX).$(2).wasm: $$(shell find $(1) -type f -not -path '*/.*' -not -name Makefile 2>/dev/null) | $(PACKS)
	$$(WASMMOD) pack $(1) -o $$@
endef
$(foreach p,$(WASM_PACKS),\
  $(eval $(call WASM_RULE,$(word 1,$(subst :, ,$(p))),$(word 2,$(subst :, ,$(p))))))

# --- elf -------------------------------------------------------------------
# $(1) source dir, $(2) pack name, $(3) arch, $(4) compiler command
# -g is not optional: Inspect resolves a symbol to a source line from the ELF's
# own DWARF (tools/inspect addr2line), and dev/tools/tests assert that path.
define ELF_RULE
$(PACKS)/$(EX).$(2).$(3).elf: $(1)/src/$(2).c $(1)/pack.toml | $(PACKS)
	$(4) -O2 -g -c -o $(PACKS)/.$(2).$(3).o $$<
	$$(WASMMOD) pack-elf --obj $(PACKS)/.$(2).$(3).o --manifest $(1)/pack.toml \
		--mpy-cross $$(MPY_CROSS) -o $$@
	rm -f $(PACKS)/.$(2).$(3).o
endef
$(foreach p,$(ELF_PACKS),\
  $(eval $(call ELF_RULE,$(word 1,$(subst :, ,$(p))),$(word 2,$(subst :, ,$(p))),x86_64,$$(CC))))
$(eval $(call ELF_RULE,hello_elf,hello,aarch64,$$(CLANG) --target=$$(AARCH64)))

# The unnamed .elf is this host's arch, so it is the same build as the arch it
# is named after — copied rather than compiled twice under two names.
$(PACKS)/$(EX).%.elf: $(PACKS)/$(EX).%.$(HOST_ARCH).elf
	cp -f $< $@

# --- sign + zlib -----------------------------------------------------------
# Sign first, then wrap: MPZL carries the signed artifact, and the loader
# unwraps before it verifies.
sign-packs: all
	for f in $(WASM_OUT) $(ELF_OUT); do \
	  $(WASMMOD) sign sign --key $(KEY) --chain $(CHAIN) "$$f" || exit 1; \
	done
	$(MAKE) $(ZLIB_OUT)
	@echo "signed $(words $(WASM_OUT) $(ELF_OUT)) packs + $(words $(ZLIB_OUT)) zlib twins"

$(PACKS)/%.zlib: $(PACKS)/%
	$(WASMMOD) zlib wrap $< -o $@

clean:
	rm -f $(WASM_OUT) $(ELF_OUT) $(ZLIB_OUT) $(PACKS)/.*.o
