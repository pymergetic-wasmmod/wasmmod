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
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_FORWARD_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_FORWARD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "extmod/wasmmod/runtime.h"

// Publish / withdraw a loaded instance for guest→guest *call* wiring.
// Module *identity* SoT is µPy sys.modules (see pm_mod.h / PACK.md).
// This list is instance/lifecycle only — prefer mp_wasm_registry_find
// which resolves via sys.modules["name"].__pack__ first.
void mp_wasm_registry_add(mp_pack_t *mod);
void mp_wasm_registry_remove(mp_pack_t *mod);
mp_pack_t *mp_wasm_registry_find(const char *name);

// Read wasmmod.imports (+ Wasm import types) and register forwarder natives.
// Must run after wasm_runtime_load and before instantiate. Idempotent per (module,func).
bool mp_wasm_register_forwarders(const uint8_t *wasm, uint32_t len, char *errbuf, size_t errbuf_len);

// After peers are registry_add'd, ensure every guest MPWI target is present.
// Peer may be a pack (__pack__ / instance list) or a __pm_modules native.
bool mp_wasm_connect_imports(const uint8_t *wasm, uint32_t len, char *errbuf, size_t errbuf_len);

// Full guest connect for a loaded pack: resolve each wasmmod.imports NEED
// via pm_mod_resolve_native and/or pack registry; install native trampolines
// for __pm_modules peers; fail if any peer is missing.
bool mp_wasm_pm_connect_guest(mp_pack_t *pack, char *errbuf, size_t errbuf_len);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_FORWARD_H
