/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Host-agnostic dependency closure API (no MicroPython / CPython types).
 */
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_RESOLVE_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_RESOLVE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef MP_WASM_DEP_NAME_MAX
#define MP_WASM_DEP_NAME_MAX (64)
#endif
#ifndef MP_WASM_DEP_VER_MAX
#define MP_WASM_DEP_VER_MAX (32)
#endif
#ifndef MP_WASM_CLOSURE_MAX
#define MP_WASM_CLOSURE_MAX (64)
#endif

typedef struct {
    char name[MP_WASM_DEP_NAME_MAX];
    char version[MP_WASM_DEP_VER_MAX];
} mp_wasm_dep_node_t;

typedef struct {
    mp_wasm_dep_node_t nodes[MP_WASM_CLOSURE_MAX];
    uint32_t n_nodes; // flat deps-first order
} mp_wasm_closure_t;

// Walk MPWD from root artifact bytes; fetch peers via CDN driver into cache.
// root_name/version identify the root; root_bytes is already fetched.
// On success, out->nodes is deps-first (leaves before dependents; cycles OK).
bool mp_wasm_resolve_closure(const char *root_name, const char *root_version,
    const uint8_t *root_bytes, uint32_t root_len,
    mp_wasm_closure_t *out, char *errbuf, size_t errbuf_len);

// Lookup cached artifact bytes for name@version (populated by resolve_closure).
// Returns NULL if missing. Pointer valid until next resolve / cache_clear.
const uint8_t *mp_wasm_closure_cached(const char *name, const char *version, uint32_t *out_len);

void mp_wasm_closure_cache_clear(void);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_RESOLVE_H
