/* MicroPython / make knobs for wasmmod (upywm).
 *
 * Enabling the module: `make MICROPY_PY_WASM=1` (see micropython.mk).
 * This header is the menuconfig surface — defaults + docs for every
 * MICROPY_PY_WASM_* / MICROPY_WASM_* switch. Override via make
 * `CFLAGS_EXTRA=-DMICROPY_PY_WASM_GEN=0` or `MICROPY_PY_WASM_GEN=0` on
 * the make command line (micropython.mk forwards the latter).
 */
#ifndef MICROPY_INCLUDED_WASMMOD_MPCONFIG_WASM_H
#define MICROPY_INCLUDED_WASMMOD_MPCONFIG_WASM_H

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

/* No OS under this image: wasmmod's POSIX io fill is left out of the build and
 * io bytes come from the image heap (MICROPY_WASM_*) rather than libc malloc.
 * Describes the image, not the module, so it is answered even where
 * MICROPY_PY_WASM is off. An emcc cell is freestanding by construction; a
 * firmware seat says so through ports/freestanding/mpconfig_freestanding.h.
 * This is the one axis those seats differ on — not a downstream's name. */
#ifndef MICROPY_WASM_FREESTANDING
#if defined(__EMSCRIPTEN__)
#define MICROPY_WASM_FREESTANDING (1)
#else
#define MICROPY_WASM_FREESTANDING (0)
#endif
#endif

/* modwasmmod.c is in the build, so `pymergetic.wasmmod` is the full face (gen,
 * test, publish, the pack/ELF machinery behind them). A seat that leaves that
 * TU out takes the smaller face from modcdn.c instead and says so here. Which
 * TUs a seat compiles is a build fact — it is not "freestanding", and a seat
 * with room for the full face may well have no OS under it. */
#ifndef MICROPY_PY_WASM_FULL
#define MICROPY_PY_WASM_FULL (1)
#endif

#if MICROPY_PY_WASM

#ifndef MICROPY_MODULE_BUILTIN_SUBPACKAGES
#define MICROPY_MODULE_BUILTIN_SUBPACKAGES (1)
#endif
#ifndef MICROPY_MODULE_BUILTIN_INIT
#define MICROPY_MODULE_BUILTIN_INIT (1)
#endif
/* Guest packs live under ROM `pymergetic`; LOAD_ATTR after
 * `import a.b.c as d` resolves `pymergetic.<leaf>` via sys.modules. */
#ifndef MICROPY_MODULE_ATTR_DELEGATION
#define MICROPY_MODULE_ATTR_DELEGATION (1)
#endif

/* The import hook must be live before user code runs. A seat that owns
 * MICROPY_PORT_INIT_FUNC calls mp_wasm_port_init() from its own init. */
void mp_wasm_port_init(void);
#ifndef MICROPY_PORT_INIT_FUNC
#define MICROPY_PORT_INIT_FUNC mp_wasm_port_init()
#endif

/* In-bin / host facegen (`pymergetic.util.gen` + cargo feature `gen`).
 * Unix product default ON. Freestanding / lean guest: set 0. */
#ifndef MICROPY_PY_WASM_GEN
#define MICROPY_PY_WASM_GEN (1)
#endif

/* ELF container load path (packbind / finder). */
#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (1)
#endif

/* Optional pack verify on load. */
#ifndef MICROPY_WASM_VERIFY
#define MICROPY_WASM_VERIFY (0)
#endif

#ifndef MICROPY_WASM_AOT_VERSION
#define MICROPY_WASM_AOT_VERSION (0)
#endif

/* Comma list baked into CFLAGS as a string when set from make. */
#ifndef MICROPY_WASM_CONTAINERS
#define MICROPY_WASM_CONTAINERS "elf,aot,wasm"
#endif

#endif /* MICROPY_PY_WASM */

#endif /* MICROPY_INCLUDED_WASMMOD_MPCONFIG_WASM_H */
