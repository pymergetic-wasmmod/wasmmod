# Build hello.elf (ELF64 ET_REL pack) for the in-tree ELF loader.
#
#   make -C extmod/wasmmod/examples/hello_elf
#   make -C extmod/wasmmod/examples/hello_elf aarch64   # cross object (CDN twin)

include ../find_top.mk
OUT = ../packs/pymergetic.wasmmod_examples.hello.elf
ARCH ?= $(shell uname -m)
OUT_ARCH = ../packs/pymergetic.wasmmod_examples.hello.$(ARCH).elf
OUT_AARCH64 = ../packs/pymergetic.wasmmod_examples.hello.aarch64.elf

CC ?= gcc
# -g keeps .debug_line for CDN/wasmmod inspect addr2line (not stripped by pack-elf).
CFLAGS ?= -ffreestanding -fPIC -fno-plt -fno-stack-protector -O2 -g -c
CLANG ?= clang
AARCH64_CFLAGS ?= -target aarch64-linux-gnu $(CFLAGS)
MPY_CROSS ?= $(if $(TOP),$(TOP)/mpy-cross/build/mpy-cross,)

.PHONY: all clean aarch64

all: $(OUT) $(OUT_ARCH)

$(OUT) $(OUT_ARCH): src/hello.c pack.toml ../hello/src/__init__.py
	mkdir -p ../packs
	$(CC) $(CFLAGS) -o /tmp/hello_elf.o src/hello.c
	MPY_CROSS="$(MPY_CROSS)" python3 $(WASMMOD) pack-elf --obj /tmp/hello_elf.o --manifest pack.toml -o $(OUT)
	cp -f $(OUT) $(OUT_ARCH)
	rm -f $(OUT).zlib $(OUT_ARCH).zlib

# Cross-built artifact for ARM hosts / CDN; not runnable on x86_64 loaders.
aarch64: src/hello.c pack.toml ../hello/src/__init__.py
	mkdir -p ../packs
	$(CLANG) $(AARCH64_CFLAGS) -o /tmp/hello_elf_aarch64.o src/hello.c
	MPY_CROSS="$(MPY_CROSS)" python3 $(WASMMOD) pack-elf --obj /tmp/hello_elf_aarch64.o --manifest pack.toml -o $(OUT_AARCH64)
	rm -f $(OUT_AARCH64).zlib

clean:
	rm -f $(OUT) $(OUT_ARCH) $(OUT_AARCH64) $(OUT).zlib $(OUT_ARCH).zlib $(OUT_AARCH64).zlib \
		/tmp/hello_elf.o /tmp/hello_elf_aarch64.o
