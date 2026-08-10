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
 * thunk.c's missing half: a pure-Python callable has no C-ABI pointer, so
 * pm_mod_resolve_native / forward_raw can't call it directly. This installs
 * one real, distinct C function per registration (X-macro pool, same
 * reasoning as thunk.c) that does mp_call_function_n_kw(callable, ...) under
 * the hood and is itself what gets published into __pm_modules[module].native
 * — so a resident Python function becomes callable from C, Rust, or a wasm
 * guest through the exact same pm_mod_resolve_native path as any other
 * export, no marshaling knowledge required at the call site.
 *
 * Bounded, not universal: 0..3 plain-int args -> int result. A callable
 * needing float/bytes/richer args, or one whose result isn't int-convertible,
 * is simply not exportable this way (mp_call_function_n_kw / attribute-based
 * calling via wasmmod.host still works for those) rather than doing something
 * silently wrong.
 */

#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_PYEXPORT_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_PYEXPORT_H

#include "py/obj.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Publish __pm_modules[module_name] (if not already present) and install a
 * native int32 trampoline for func_name that calls `callable` with `nargs`
 * (0..3) int args and converts its return value with mp_obj_get_int.
 * Returns 0 on success, -1 if nargs is unsupported or every slot for that
 * arity is already in use.
 */
int pm_mod_export_py(const char *module_name, const char *func_name,
    mp_obj_t callable, uint32_t nargs);

/*
 * Same idea, richer single-argument shapes — one arg of the named type,
 * result converted back with the matching mp_obj_new_ / mp_obj_get_ pair.
 * Each has its own bounded pool, same reasoning as pm_mod_export_py.
 */
int pm_mod_export_py_i64(const char *module_name, const char *func_name, mp_obj_t callable);
int pm_mod_export_py_f32(const char *module_name, const char *func_name, mp_obj_t callable);
int pm_mod_export_py_f64(const char *module_name, const char *func_name, mp_obj_t callable);

/*
 * (int32 cookie) -> int32. Looks the cookie up via mp_wasm_mem_data, wraps it
 * as a bytes object, calls `callable(buf)`, converts the int result. Lets a
 * guest hand the host a durable memory-cookie (wasm.mem_alloc/mem_copy_in)
 * and reach a plain-Python callback with it — no exec_env needed since the
 * cookie table isn't tied to any particular calling module's instance.
 */
int pm_mod_export_py_mem(const char *module_name, const char *func_name, mp_obj_t callable);

/*
 * (int32 handle) -> int32. Resolves the handle via mp_wasm_handle_resolve,
 * calls `callable(obj)`, converts the int result. Same no-exec_env reasoning
 * as pm_mod_export_py_mem, for the wasm.handle_register/handle_free table.
 */
int pm_mod_export_py_obj(const char *module_name, const char *func_name, mp_obj_t callable);

/*
 * (const void *ptr, int32 len) -> int32. Wraps [ptr,len) as a bytes object
 * and calls `callable(buf)`. ptr must already be a real host address — this
 * is the ELF/native-guest shape (same address space, no linear-memory
 * indirection) and is NOT safe to resolve for a wasm/WAMR guest, whose i32
 * "pointer" args are offsets into its own sandboxed linear memory, not host
 * addresses. Only wire this one up for ELF-side imports.
 */
int pm_mod_export_py_bufptr(const char *module_name, const char *func_name, mp_obj_t callable);

#ifdef __cplusplus
}
#endif

#endif /* MICROPY_INCLUDED_EXTMOD_WASMMOD_PYEXPORT_H */
