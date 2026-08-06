# Build client.elf (ELF64 ET_REL; imports hello from peer pack).
#
#   make -C extmod/wasmmod/examples/hello_elf
#   make -C extmod/wasmmod/examples/client_elf

include ../find_top.mk
OUT = ../packs/pymergetic.wasmmod_examples.client.elf
ARCH ?= $(shell uname -m)
OUT_ARCH = ../packs/pymergetic.wasmmod_examples.client.$(ARCH).elf

CC ?= gcc
CFLAGS ?= -ffreestanding -fPIC -fno-plt -fno-stack-protector -O2 -g -c

.PHONY: all clean

all: $(OUT) $(OUT_ARCH)

$(OUT) $(OUT_ARCH): src/client.c pack.toml
	mkdir -p ../packs
	$(CC) $(CFLAGS) -o /tmp/client_elf.o src/client.c
	python3 $(WASMMOD) pack-elf --obj /tmp/client_elf.o --manifest pack.toml -o $(OUT)
	cp -f $(OUT) $(OUT_ARCH)
	rm -f $(OUT).zlib $(OUT_ARCH).zlib

clean:
	rm -f $(OUT) $(OUT_ARCH) $(OUT).zlib $(OUT_ARCH).zlib /tmp/client_elf.o
