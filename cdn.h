/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Host-agnostic CDN resolve API (no MicroPython / CPython types).
 * Bindings call these from wasmmod.c / future cpy module.
 *
 * Multiple metal-cdn bases: try in order (first hit wins). Platform default
 * is usually last; site/iPXE CDNs are prepended (add) or replace the list.
 */
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_CDN_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_CDN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef MP_WASM_CDN_BASES_MAX
#define MP_WASM_CDN_BASES_MAX (4)
#endif

typedef enum {
    MP_WASM_CDN_DRIVER_PATH = 0,
    MP_WASM_CDN_DRIVER_METAL = 1,
} mp_wasm_cdn_driver_t;

// Reset list, then set primary base (compat: single-CDN bind).
// token may be NULL (public lead/pin). Copies strings internally.
void mp_wasm_cdn_configure(const char *base_url, const char *token);
void mp_wasm_cdn_reset(void);

// Append a metal-cdn base (no-op if full or duplicate). Returns false if not added.
bool mp_wasm_cdn_add(const char *base_url, const char *token);
// Prepend (site CDN first). Shifts existing bases right. Returns false if not added.
bool mp_wasm_cdn_prepend(const char *base_url, const char *token);

mp_wasm_cdn_driver_t mp_wasm_cdn_driver(void);
bool mp_wasm_cdn_require_explicit_deps(void);
const char *mp_wasm_cdn_driver_name(void);
// Primary base URL (no trailing slash), or NULL if unset.
const char *mp_wasm_cdn_base(void);
unsigned mp_wasm_cdn_base_count(void);
const char *mp_wasm_cdn_base_at(unsigned index);
// True if url equals any configured base (trailing slashes ignored).
bool mp_wasm_cdn_url_is_base(const char *url);

bool mp_wasm_cdn_fetch_pack(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len);
bool mp_wasm_cdn_fetch_pack_ex(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len,
    char *out_origin, size_t origin_len,
    char *errbuf, size_t errbuf_len);

bool mp_wasm_cdn_fetch_index(const char *channel,
    uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len);

bool mp_wasm_cdn_publish(const char *name, const char *version,
    const uint8_t *data, uint32_t data_len,
    bool lead, bool pin, const char *token,
    char *errbuf, size_t errbuf_len);

void mp_wasm_cdn_set_session_id(const char *session_id);
const char *mp_wasm_cdn_session_id(void);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_CDN_H
