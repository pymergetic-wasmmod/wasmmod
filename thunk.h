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
 *
 * Host↔guest native thunks: one real, distinct C function per numeric wasm
 * export, so any native caller (host or guest, any language) resolves it
 * through pm_mod_resolve_native exactly like a C/Rust export and calls it
 * directly, cast to its real signature — no marshaling at the call site.
 *
 * A wasm export can't sit in __pm_modules as a bare function pointer the
 * way a C/Rust export can: calling it needs the wasm runtime's own
 * marshaling (mp_pack_call_fn). A thunk is that marshaling, baked into a
 * real function with the export's own signature, so the caller-side story
 * (cast pointer, call directly) stays identical for every container type.
 *
 * Bounded, not universal: covers every numeric shape actually used by
 * wasmmod's own examples (hello/mixed/bridge/client) — 0..3 i32 params to
 * i32 result, 1 i64->i64, 1 f32->f32, 1 f64->f64, 3 f64->f64. An export
 * whose shape isn't covered is simply not thunked (falls back to
 * Python-only calling, same as before this existed) rather than silently
 * doing something wrong.
 */
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_THUNK_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_THUNK_H

#include "extmod/wasmmod/runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Install a native thunk for one numeric export and publish it via
 * pm_mod_export_set(pm_module_name, pm_func_name, thunk) — same
 * registration call every C/Rust/Python export already uses. No-op if the
 * export's real (param, result) shape (introspected via mp_pack_func_types,
 * looked up by wasm_export_name — the real wasm binary symbol) isn't in
 * the covered set, or if every slot for that shape is already taken.
 *
 * wasm_export_name and pm_func_name differ exactly when a manifest's
 * [[exports]] aliases func != export (rare — none of wasmmod's own
 * examples do); when there is no aliasing, pass the same name for both.
 * pm_module_name is the dotted name the export is reachable under in
 * __pm_modules — the pack's own name, or "pack.submodule" for a
 * [[exports]] module= route.
 */
void pm_mod_thunk_export(mp_pack_t *wmod, const char *pm_module_name,
    const char *wasm_export_name, const char *pm_func_name);

/**
 * Free every thunk slot currently pointing at `wmod`. Call on unload,
 * before (or alongside) pm_mod_unpublish — a freed slot's thunk function
 * still exists (it's a static function, not code we can unmap) but goes
 * back to "unused" and stops calling into the closed pack.
 */
void pm_mod_thunk_release_pack(mp_pack_t *wmod);

#ifdef __cplusplus
}
#endif

#endif /* MICROPY_INCLUDED_EXTMOD_WASMMOD_THUNK_H */
