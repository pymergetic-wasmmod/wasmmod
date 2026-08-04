/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Host-agnostic CDN resolve API (no MicroPython / CPython types).
 * Bindings call these from wasmmod.c / future cpy module.
 */
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_CDN_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_CDN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MP_WASM_CDN_DRIVER_PATH = 0,
    MP_WASM_CDN_DRIVER_METAL = 1,
} mp_wasm_cdn_driver_t;

// Bind metal-cdn driver to base_url (artifacts/index under that prefix).
// token may be NULL (public lead/pin). Copies strings internally.
// Flat HTTP pack mirrors belong on wasm.path via install_hook / path.append — not here.
void mp_wasm_cdn_configure(const char *base_url, const char *token);
void mp_wasm_cdn_reset(void);

mp_wasm_cdn_driver_t mp_wasm_cdn_driver(void);
bool mp_wasm_cdn_require_explicit_deps(void);
const char *mp_wasm_cdn_driver_name(void);
// Configured base URL (no trailing slash), or NULL if unset.
const char *mp_wasm_cdn_base(void);
// True if url equals the configured base (trailing slashes ignored).
bool mp_wasm_cdn_url_is_base(const char *url);

// Resolve name@version into artifact bytes (tries pin then lead / path candidates).
// On success: *out_bytes is MICROPY_WASM_MALLOC'd; caller MICROPY_WASM_FREE(*out_bytes).
bool mp_wasm_cdn_fetch_pack(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len);

// GET {base}/index/{channel} raw JSON bytes (metal driver only).
// channel NULL/"lead" → lead; "@1.0" or "pin/1.0" → pin index. Caller frees *out_bytes.
bool mp_wasm_cdn_fetch_index(const char *channel,
    uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len);

// Publish stub — returns false until io ops.request is wired (browser POST).
bool mp_wasm_cdn_publish(const char *name, const char *version,
    const uint8_t *data, uint32_t data_len,
    bool lead, bool pin, const char *token,
    char *errbuf, size_t errbuf_len);

// Process-global shell/loader session id (correlation header / autoexec bind).
void mp_wasm_cdn_set_session_id(const char *session_id);
const char *mp_wasm_cdn_session_id(void);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_CDN_H
