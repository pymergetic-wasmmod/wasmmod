# Source list for MicroPython / metalpython extmod glue.
# Expect this repo checked out as $(WASMMOD_DIR), default extmod/wasmmod.
WASMMOD_DIR ?= extmod/wasmmod

SRC_WASMMOD = \
	$(WASMMOD_DIR)/wasmmod.c \
	$(WASMMOD_DIR)/fetch.c \
	$(WASMMOD_DIR)/finder.c \
	$(WASMMOD_DIR)/forward.c \
	$(WASMMOD_DIR)/host.c \
	$(WASMMOD_DIR)/pack.c \
	$(WASMMOD_DIR)/runtime.c \
	$(WASMMOD_DIR)/verify.c
