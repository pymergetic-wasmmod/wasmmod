/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * Host-side pack inspect (symbols / DWARF hooks). Mirror of tools/wasmmod_inspect.py.
 */
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_INSPECT_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_INSPECT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#ifndef MP_WASM_INSPECT_NAME_MAX
#define MP_WASM_INSPECT_NAME_MAX (96)
#endif

typedef struct mp_wasm_sym_t {
    char name[MP_WASM_INSPECT_NAME_MAX];
    int32_t section_index; // -1 if n/a
    uint64_t offset;
    uint64_t size;
    uint8_t kind; // 0=other 1=func 2=data 3=export
    uint8_t binding; // 0=local 1=global 2=weak 3=export
} mp_wasm_sym_t;

typedef struct mp_wasm_loc_t {
    char path[MP_WASM_INSPECT_NAME_MAX];
    int32_t line; // -1 if unknown
    uint8_t role; // 0=sym 1=dwarf 2=def 3=decl 4=twin
} mp_wasm_loc_t;

bool mp_wasm_inspect_has_dwarf(const uint8_t *buf, uint32_t len);

// Fill out[0..cap); returns false on truncate/error. *n_out = written count.
bool mp_wasm_inspect_symbols(const uint8_t *buf, uint32_t len,
    mp_wasm_sym_t *out, size_t cap, size_t *n_out);

bool mp_wasm_inspect_addr2line(const uint8_t *buf, uint32_t len, uint64_t addr,
    mp_wasm_loc_t *out, size_t cap, size_t *n_out);

#endif // MICROPY_PY_WASM
#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_INSPECT_H
