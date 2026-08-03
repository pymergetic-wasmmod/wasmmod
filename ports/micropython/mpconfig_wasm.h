/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

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

// Optional default wasm.arch entry for AOT filename tags (e.g. "x86_64"); empty = plain .aot names.
#ifndef MICROPY_WASM_PACK_ARCH
#define MICROPY_WASM_PACK_ARCH ""
#endif

// Replaceable host I/O / alloc / verify — see extmod/wasmmod/ports/PORT.md
// and extmod/wasmmod/io.h (mp_wasm_io_ops_t, mp_wasm_io_set).
//
// Bake root CA(s) at image build (zlib ROM; lazy on first verify / trust_count):
//   make MICROPY_WASM_TRUST_CA="a.der b.der" …
// Or from the port (flash / ROM bytes), via ensure / TRUST_BOOT:
//   #define MICROPY_WASM_TRUST_BOOT() do { mp_wasm_trust_add(ca, ca_len); } while (0)

#endif // MICROPY_INCLUDED_WASMMOD_MPCONFIG_WASM_H
