# CPython host face for wasmmod.
#
#   make -C ports/cpython
#   make -C ports/cpython smoke

WASMMOD_ROOT := $(abspath ../..)
BUILD := $(CURDIR)/build
PYTHON ?= python3
CC ?= gcc

EXT_SUFFIX := $(shell $(PYTHON) -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))")
PY_INCLUDES := $(shell $(PYTHON)-config --includes)
PY_LDFLAGS := $(shell $(PYTHON)-config --ldflags --embed 2>/dev/null || $(PYTHON)-config --ldflags)

# CPython requires bundled TLS; do not reuse a µPy --no-default-features archive.
CARGO_TARGET := $(BUILD)/cargo/release
STATICLIB := $(CARGO_TARGET)/libpymergetic_wasmmod.a
# Expand after Cargo runs so the first build can discover its WAMR archive.
IWASM_A = $(firstword $(wildcard $(CARGO_TARGET)/build/*/out/vmlib/build/libiwasm.a))

CFLAGS := -fPIC -O2 -Wall -Wextra \
	-I$(WASMMOD_ROOT) -I$(WASMMOD_ROOT)/src \
	-I$(CURDIR)/stubs \
	-I$(WASMMOD_ROOT)/../.. \
	-I$(WASMMOD_ROOT)/../../lib/uzlib \
	-DPM_WASMMOD_CPYTHON=1 -DPM_WASMMOD_GUEST=0 \
	-DMICROPY_PY_DEFLATE=1 \
	-DMICROPY_PY_WASM_ELF=1 \
	-DMICROPY_WASM_CONTAINERS=\"elf,aot,wasm\" \
	-DPM_WASMMOD_HOST_SRC=\"$(WASMMOD_ROOT)/src\" \
	$(PY_INCLUDES)

PORT_SRCS := \
	_wasmmod.c \
	objhandle.c \
	nativecall.c \
	hostready.c \
	importhook.c \
	pyexport.c \
	finder.c \
	packbind.c \
	modgen.c

COMMON_SRCS := \
	$(WASMMOD_ROOT)/ports/common/boot.c \
	$(WASMMOD_ROOT)/ports/common/load.c \
	$(WASMMOD_ROOT)/ports/common/memcookie.c

CORE_SRCS := \
	$(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/manifest.c \
	$(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/zlib_env.c \
	$(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/format/common/format.c \
	$(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/format/wasm/section.c \
	$(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/format/aot/section.c \
	$(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/format/elf/section.c \
	$(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/format/elf/load.c \
	$(WASMMOD_ROOT)/src/pymergetic/wasmmod/verify/__impl__.c

PORT_OBJS := $(addprefix $(BUILD)/obj/port/, $(PORT_SRCS:.c=.o))
COMMON_OBJS := $(BUILD)/obj/common/boot.o $(BUILD)/obj/common/load.o $(BUILD)/obj/common/memcookie.o
CORE_OBJS := \
	$(BUILD)/obj/pack/manifest.o \
	$(BUILD)/obj/pack/zlib_env.o \
	$(BUILD)/obj/pack/format.o \
	$(BUILD)/obj/pack/wasm_section.o \
	$(BUILD)/obj/pack/aot_section.o \
	$(BUILD)/obj/pack/elf_section.o \
	$(BUILD)/obj/pack/elf_load.o \
	$(BUILD)/obj/uzlib/header.o \
	$(BUILD)/obj/verify/impl.o

OBJS := $(PORT_OBJS) $(COMMON_OBJS) $(CORE_OBJS)
MOD := $(BUILD)/pymergetic/wasmmod$(EXT_SUFFIX)

.PHONY: all clean smoke staticlib FORCE
all: $(MOD)

staticlib: $(STATICLIB)

FORCE:

# Cargo tracks source/feature freshness; archive existence alone does not.
$(STATICLIB): FORCE
	cd $(WASMMOD_ROOT) && cargo build --release --features upy-host,bundle-mbedtls --target-dir $(BUILD)/cargo

$(BUILD)/obj/port $(BUILD)/obj/common $(BUILD)/obj/pack $(BUILD)/obj/verify $(BUILD)/obj/uzlib $(BUILD)/pymergetic:
	mkdir -p $@

$(BUILD)/pymergetic/__init__.py: | $(BUILD)/pymergetic
	printf '# CPython wasmmod host package shell\n' > $@

$(BUILD)/obj/port/%.o: %.c | $(BUILD)/obj/port
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/obj/common/%.o: $(WASMMOD_ROOT)/ports/common/%.c | $(BUILD)/obj/common
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/obj/pack/manifest.o: $(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/manifest.c | $(BUILD)/obj/pack
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD)/obj/pack/zlib_env.o: $(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/zlib_env.c | $(BUILD)/obj/pack
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD)/obj/pack/format.o: $(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/format/common/format.c | $(BUILD)/obj/pack
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD)/obj/pack/wasm_section.o: $(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/format/wasm/section.c | $(BUILD)/obj/pack
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD)/obj/pack/aot_section.o: $(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/format/aot/section.c | $(BUILD)/obj/pack
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD)/obj/pack/elf_section.o: $(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/format/elf/section.c | $(BUILD)/obj/pack
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD)/obj/pack/elf_load.o: $(WASMMOD_ROOT)/src/pymergetic/wasmmod/pack/format/elf/load.c | $(BUILD)/obj/pack
	$(CC) $(CFLAGS) -c -o $@ $<
# The staticlib's util.zlib supplies inflate and Adler/CRC checksums once.
# Only the pack header parser is specific to this host link.
$(BUILD)/obj/uzlib/header.o: $(WASMMOD_ROOT)/../../lib/uzlib/header.c | $(BUILD)/obj/uzlib
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD)/obj/verify/impl.o: $(WASMMOD_ROOT)/src/pymergetic/wasmmod/verify/__impl__.c | $(BUILD)/obj/verify
	$(CC) $(CFLAGS) -c -o $@ $<

$(MOD): $(OBJS) $(STATICLIB) $(BUILD)/pymergetic/__init__.py
	$(CC) -shared -o $@ $(OBJS) \
		-Wl,--whole-archive $(STATICLIB) -Wl,--no-whole-archive \
		$(if $(IWASM_A),-L$(dir $(IWASM_A)) -liwasm) \
		-lpthread -ldl -lm -lstdc++ $(PY_LDFLAGS)

smoke: $(MOD)
	PYTHONPATH=$(BUILD) $(PYTHON) -c "\
import pymergetic.wasmmod as w; \
print('version', w.version()); \
import pymergetic.util.pysample as s; \
print('hello', s.hello()); \
print('call', w.call('pymergetic.util.pysample', 'pm_util_pysample_hello')); \
print('echo', w.call('pymergetic.util.pysample', 'pm_util_pysample_echo_len', [b'abc'])); \
print('mem', w.call('pymergetic.util.pysample', 'pm_util_pysample_echo_mem', [b'xy'])); \
print('none', w.call('pymergetic.util.pysample', 'pm_util_pysample_is_none', [None])); \
print('connect', w.connect('pymergetic.util.pysample', 'pm_util_pysample_hello')); \
import tempfile, os, pymergetic.util.gen as g; \
d = tempfile.mkdtemp(); \
rc = g.run_vfs(d, 'pymergetic.util.version'); \
h = open(os.path.join(d, '__exports__.h')).read(); \
print('run_vfs', rc, 'satisfies' in h); \
print('check', g.run_vfs(d, 'pymergetic.util.version', True)); \
print('diff', g.diff('pymergetic.util.version', h), g.diff('pymergetic.util.version', 'nope')); \
"

clean:
	rm -rf $(BUILD)
