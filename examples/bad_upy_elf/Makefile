# Build badupy.elf under /tmp for the negative micropython.* load test.
include ../find_top.mk
OUT ?= /tmp/wasmmod_badupy.elf
CC ?= gcc
CFLAGS ?= -ffreestanding -fPIC -fno-plt -fno-stack-protector -O2 -c

.PHONY: all clean

all: $(OUT)

$(OUT): src/bad.c pack.toml
	$(CC) $(CFLAGS) -o /tmp/bad_upy_elf.o src/bad.c
	python3 $(WASMMOD) pack-elf --obj /tmp/bad_upy_elf.o --manifest pack.toml -o $(OUT)

clean:
	rm -f $(OUT) /tmp/bad_upy_elf.o
