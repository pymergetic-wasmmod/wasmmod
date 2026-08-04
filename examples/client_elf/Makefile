# Build client.elf (ELF64 ET_REL; imports hello from peer pack).
#
#   make -C extmod/wasmmod/examples/hello_elf
#   make -C extmod/wasmmod/examples/client_elf

include ../find_top.mk
OUT = ../packs/client.elf
ARCH ?= $(shell uname -m)
OUT_ARCH = ../packs/client.$(ARCH).elf

CC ?= gcc
CFLAGS ?= -ffreestanding -fPIC -fno-plt -fno-stack-protector -O2 -c

.PHONY: all clean

all: $(OUT) $(OUT_ARCH)

$(OUT) $(OUT_ARCH): src/client.c pack.toml
	mkdir -p ../packs
	$(CC) $(CFLAGS) -o /tmp/client_elf.o src/client.c
	python3 $(WASMMOD) pack-elf --obj /tmp/client_elf.o --manifest pack.toml -o $(OUT)
	cp -f $(OUT) $(OUT_ARCH)

clean:
	rm -f $(OUT) $(OUT_ARCH) /tmp/client_elf.o
