/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_wasmmod/path/cdn.h"
#include "cdn.h"
void pm_wasmmod_cdn_configure(const char *base_url, const char *token) { mp_wasm_cdn_configure(base_url, token); }
void pm_wasmmod_cdn_reset(void) { mp_wasm_cdn_reset(); }
bool pm_wasmmod_cdn_add(const char *base_url, const char *token) { return mp_wasm_cdn_add(base_url, token); }
bool pm_wasmmod_cdn_prepend(const char *base_url, const char *token) { return mp_wasm_cdn_prepend(base_url, token); }
bool pm_wasmmod_cdn_fetch_pack(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len, char *errbuf, size_t errbuf_len) {
    return mp_wasm_cdn_fetch_pack(name, version, out_bytes, out_len, errbuf, errbuf_len);
}
bool pm_wasmmod_cdn_fetch_index(const char *channel,
    uint8_t **out_bytes, uint32_t *out_len, char *errbuf, size_t errbuf_len) {
    return mp_wasm_cdn_fetch_index(channel, out_bytes, out_len, errbuf, errbuf_len);
}

