/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_wasmmod/inspect/inspect.h"

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM 0
#endif

#if MICROPY_PY_WASM
#include "inspect.h"
_Static_assert(sizeof(pm_wasmmod_sym_t) == sizeof(mp_wasm_sym_t), "sym layout");
_Static_assert(sizeof(pm_wasmmod_loc_t) == sizeof(mp_wasm_loc_t), "loc layout");
_Static_assert(sizeof(pm_wasmmod_disasm_line_t) == sizeof(mp_wasm_disasm_line_t), "disasm layout");
#endif

#include <stddef.h>

bool pm_wasmmod_inspect_has_dwarf(const uint8_t *buf, uint32_t len) {
#if MICROPY_PY_WASM
    return mp_wasm_inspect_has_dwarf(buf, len);
#else
    (void)buf;
    (void)len;
    return false;
#endif
}

bool pm_wasmmod_inspect_symbols(const uint8_t *buf, uint32_t len,
    pm_wasmmod_sym_t *out, size_t cap, size_t *n_out) {
#if MICROPY_PY_WASM
    return mp_wasm_inspect_symbols(buf, len, (mp_wasm_sym_t *)out, cap, n_out);
#else
    (void)buf;
    (void)len;
    (void)out;
    (void)cap;
    if (n_out) {
        *n_out = 0;
    }
    return false;
#endif
}

bool pm_wasmmod_inspect_addr2line(const uint8_t *buf, uint32_t len, uint64_t addr,
    pm_wasmmod_loc_t *out, size_t cap, size_t *n_out) {
#if MICROPY_PY_WASM
    return mp_wasm_inspect_addr2line(buf, len, addr, (mp_wasm_loc_t *)out, cap, n_out);
#else
    (void)buf;
    (void)len;
    (void)addr;
    (void)out;
    (void)cap;
    if (n_out) {
        *n_out = 0;
    }
    return false;
#endif
}

bool pm_wasmmod_inspect_locations(const uint8_t *buf, uint32_t len, const char *name,
    pm_wasmmod_loc_t *out, size_t cap, size_t *n_out) {
#if MICROPY_PY_WASM
    return mp_wasm_inspect_locations(buf, len, name, (mp_wasm_loc_t *)out, cap, n_out);
#else
    (void)buf;
    (void)len;
    (void)name;
    (void)out;
    (void)cap;
    if (n_out) {
        *n_out = 0;
    }
    return false;
#endif
}

bool pm_wasmmod_inspect_disasm(const uint8_t *buf, uint32_t len,
    uint32_t section_index, uint32_t offset, uint32_t limit,
    pm_wasmmod_disasm_line_t *out, size_t cap, size_t *n_out) {
#if MICROPY_PY_WASM
    return mp_wasm_inspect_disasm(buf, len, section_index, offset, limit,
        (mp_wasm_disasm_line_t *)out, cap, n_out);
#else
    (void)buf;
    (void)len;
    (void)section_index;
    (void)offset;
    (void)limit;
    (void)out;
    (void)cap;
    if (n_out) {
        *n_out = 0;
    }
    return false;
#endif
}

bool pm_wasmmod_inspect_mpy_disasm(const uint8_t *mpy, uint32_t len, uint32_t limit,
    pm_wasmmod_disasm_line_t *out, size_t cap, size_t *n_out) {
#if MICROPY_PY_WASM
    return mp_wasm_inspect_mpy_disasm(mpy, len, limit, (mp_wasm_disasm_line_t *)out, cap, n_out);
#else
    (void)mpy;
    (void)len;
    (void)limit;
    (void)out;
    (void)cap;
    if (n_out) {
        *n_out = 0;
    }
    return false;
#endif
}
