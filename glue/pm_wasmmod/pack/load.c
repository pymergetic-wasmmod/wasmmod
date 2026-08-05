/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_wasmmod/pack/load.h"
#include "runtime.h"

/* pm_wasmmod_pack_t is mp_pack_t. */
pm_wasmmod_pack_t *pm_wasmmod_pack_load(const uint8_t *bytes, uint32_t len,
    const char *name, char *errbuf, size_t errbuf_len) {
    return (pm_wasmmod_pack_t *)mp_pack_load(bytes, len, name, errbuf, errbuf_len);
}
pm_wasmmod_pack_t *pm_wasmmod_pack_load_ex(const uint8_t *code, uint32_t code_len,
    const uint8_t *meta, uint32_t meta_len, const char *name, const char *path_hint,
    char *errbuf, size_t errbuf_len) {
    return (pm_wasmmod_pack_t *)mp_pack_load_ex(code, code_len, meta, meta_len, name, path_hint, errbuf, errbuf_len);
}
void pm_wasmmod_pack_close(pm_wasmmod_pack_t *pack) { mp_pack_close((mp_pack_t *)pack); }
const char *pm_wasmmod_pack_kind_str(const pm_wasmmod_pack_t *pack) { return mp_pack_kind_str((const mp_pack_t *)pack); }
const char *pm_wasmmod_pack_origin(const pm_wasmmod_pack_t *pack) { return mp_pack_origin((const mp_pack_t *)pack); }
const char *pm_wasmmod_pack_arch(const pm_wasmmod_pack_t *pack) { return mp_pack_arch((const mp_pack_t *)pack); }

