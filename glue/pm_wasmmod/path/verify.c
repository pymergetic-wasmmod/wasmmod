/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_wasmmod/path/verify.h"
#include "verify.h"

bool pm_wasmmod_trust_add(const uint8_t *key, size_t key_len) {
    return mp_wasm_trust_add(key, key_len);
}
void pm_wasmmod_trust_clear(void) {
    mp_wasm_trust_clear();
}
size_t pm_wasmmod_trust_count(void) {
    return mp_wasm_trust_count();
}
bool pm_wasmmod_verify_bytes(const uint8_t *bytes, uint32_t len,
    const char *path_hint, char *errbuf, size_t errbuf_len) {
    return mp_wasm_verify_bytes(bytes, len, path_hint, errbuf, errbuf_len);
}
void pm_wasmmod_set_verify_enabled(bool on) {
    mp_wasm_set_verify_enabled(on);
}
bool pm_wasmmod_get_verify_enabled(void) {
    return mp_wasm_get_verify_enabled();
}
