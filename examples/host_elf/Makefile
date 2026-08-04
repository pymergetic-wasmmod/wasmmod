# Build hostcall.elf (ELF guest → wasmmod.host).
#
#   make -C extmod/wasmmod/examples/host_elf

include ../find_top.mk
OUT = ../packs/hostcall.elf
ARCH ?= $(shell uname -m)
OUT_ARCH = ../packs/hostcall.$(ARCH).elf

CC ?= gcc
CFLAGS ?= -ffreestanding -fPIC -fno-plt -fno-stack-protector -O2 -c

.PHONY: all clean

all: $(OUT) $(OUT_ARCH)

$(OUT) $(OUT_ARCH): src/hostcall.c pack.toml
	mkdir -p ../packs
	$(CC) $(CFLAGS) -o /tmp/hostcall_elf.o src/hostcall.c
	python3 $(WASMMOD) pack-elf --obj /tmp/hostcall_elf.o --manifest pack.toml -o $(OUT)
	cp -f $(OUT) $(OUT_ARCH)
	rm -f $(OUT).zlib $(OUT_ARCH).zlib

clean:
	rm -f $(OUT) $(OUT_ARCH) $(OUT).zlib $(OUT_ARCH).zlib /tmp/hostcall_elf.o
