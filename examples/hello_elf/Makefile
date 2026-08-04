# Build hello.elf (ELF64 ET_REL pack) for the in-tree ELF loader.
#
#   make -C extmod/wasmmod/examples/hello_elf

include ../find_top.mk
OUT = ../packs/hello.elf
ARCH ?= $(shell uname -m)
OUT_ARCH = ../packs/hello.$(ARCH).elf

CC ?= gcc
CFLAGS ?= -ffreestanding -fno-pic -fno-plt -fno-stack-protector -O2 -c

.PHONY: all clean

all: $(OUT) $(OUT_ARCH)

$(OUT) $(OUT_ARCH): src/hello.c pack.toml
	mkdir -p ../packs
	$(CC) $(CFLAGS) -o /tmp/hello_elf.o src/hello.c
	python3 $(WASMMOD) pack-elf --obj /tmp/hello_elf.o --manifest pack.toml -o $(OUT)
	cp -f $(OUT) $(OUT_ARCH)

clean:
	rm -f $(OUT) $(OUT_ARCH) /tmp/hello_elf.o
