/* MicroPython / make knobs for wasmmod (mpwm).
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

#if MICROPY_PY_WASM

#ifndef MICROPY_MODULE_BUILTIN_SUBPACKAGES
#define MICROPY_MODULE_BUILTIN_SUBPACKAGES (1)
#endif
#ifndef MICROPY_MODULE_BUILTIN_INIT
#define MICROPY_MODULE_BUILTIN_INIT (1)
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
