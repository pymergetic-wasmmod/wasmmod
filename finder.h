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
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_FINDER_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_FINDER_H

#include <stddef.h>
#include <stdbool.h>

#include "py/obj.h"

// Search wasm.path then sys.path for a pack matching the dotted import name.
// Path form: a/b/c/__init__.wasm | a/b/c.wasm
// Flat form: a.b.c.wasm  (when name has dots; handy under packs/)
// With AOT: try a/b/c[.<arch>].aot{N} / a.b.c[.<arch>].aot{N} where N is
// WAMR AOT_CURRENT_VERSION (file format), then legacy .aot, then .wasm.
// Optional trailing .zlib preferred for all candidates.
// On success writes *path_out (caller vstr_clear).
bool mp_wasm_find_pack(const char *dotted_name, vstr_t *path_out);

// Like find_pack but only searches wasm.path (cheap import-hook pre-check).
bool mp_wasm_find_pack_on_wasm_path(const char *dotted_name, vstr_t *path_out);

// True if a leaf pack exists or VFS pack roots imply descendants (flat/tree).
bool mp_wasm_query_import(const char *dotted_name);
bool mp_wasm_has_descendants(const char *prefix);

// Load-or-reuse a pack by dotted import name. Raises on failure.
// Namespace packages (PEP 420-ish) are created when only children exist.
// If path is non-NULL, skip find_pack and load that artifact (import-hook reuse).
mp_obj_t mp_wasm_import_wasm(const char *dotted_name);
mp_obj_t mp_wasm_import_wasm_at(const char *dotted_name, const char *path);

// Path / arch lists (wasm.path, wasm.arch) and pack load entry used by finder.
void mp_wasm_path_ensure(void);
mp_obj_t mp_wasm_path_obj(void);
void mp_wasm_arch_ensure(void);
mp_obj_t mp_wasm_arch_obj(void);
mp_obj_t mp_wasm_load_pack_path(const char *path, const char *name_override);

// Host AOT file-format version (WAMR AOT_CURRENT_VERSION), or 0 if unavailable.
unsigned mp_wasm_aot_format_version(void);
// Length of trailing .aot / .aot<digits> suffix in s[0..n), or 0.
size_t mp_wasm_aot_suffix_len_n(const char *s, size_t n);
size_t mp_wasm_aot_suffix_len(const char *s);
bool mp_wasm_path_is_aot(const char *path);
// Write host format ext into buf (e.g. ".aot6"); falls back to ".aot".
void mp_wasm_aot_format_ext(char *buf, size_t buflen);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_FINDER_H
