# Build ticks.elf — positive micropython.runtime.ticks_ms catalog smoke.
include ../find_top.mk
OUT = ../packs/ticks.elf
CC ?= gcc
CFLAGS ?= -ffreestanding -fPIC -fno-plt -fno-stack-protector -O2 -g -c

.PHONY: all clean

all: $(OUT)

$(OUT): src/ticks.c pack.toml
	mkdir -p ../packs
	$(CC) $(CFLAGS) -o /tmp/ticks_elf.o src/ticks.c
	python3 $(WASMMOD) pack-elf --obj /tmp/ticks_elf.o --manifest pack.toml -o $(OUT)
	rm -f $(OUT).zlib

clean:
	rm -f $(OUT) $(OUT).zlib /tmp/ticks_elf.o
