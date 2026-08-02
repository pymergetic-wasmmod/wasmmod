/*
 * Optional MicroPython port defaults for wasmmod.
 *
 * Not required: enabling is `make MICROPY_PY_WASM=1`, which injects -D flags
 * via micropython.mk. Include this from a port's mpconfigport.h only if you
 * want documented C defaults without relying on the make fragment:
 *
 *   #include "extmod/wasmmod/ports/micropython/mpconfig_wasm.h"
 */
#ifndef MICROPY_INCLUDED_WASMMOD_MPCONFIG_WASM_H
#define MICROPY_INCLUDED_WASMMOD_MPCONFIG_WASM_H

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#ifndef MICROPY_PY_WASM_AOT
#define MICROPY_PY_WASM_AOT (0)
#endif

#ifndef MICROPY_PY_WASM_JIT
#define MICROPY_PY_WASM_JIT (0)
#endif

#ifndef MICROPY_PY_WASM_FAST_JIT
#define MICROPY_PY_WASM_FAST_JIT (0)
#endif

#ifndef MICROPY_PY_WASM_MATRIX
#define MICROPY_PY_WASM_MATRIX (0)
#endif

#ifndef MICROPY_WASM_VERIFY
#define MICROPY_WASM_VERIFY (0)
#endif

#endif // MICROPY_INCLUDED_WASMMOD_MPCONFIG_WASM_H
