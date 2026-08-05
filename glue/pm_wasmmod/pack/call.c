/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_wasmmod/pack/call.h"
#include "runtime.h"
bool pm_wasmmod_pack_call_0(pm_wasmmod_pack_t *pack, const char *func,
    int32_t *out_result, char *errbuf, size_t errbuf_len) {
    return mp_pack_call0((mp_pack_t *)pack, func, out_result, errbuf, errbuf_len);
}
bool pm_wasmmod_pack_call_i32(pm_wasmmod_pack_t *pack, const char *func,
    const int32_t *args, uint32_t nargs, int32_t *out_result,
    char *errbuf, size_t errbuf_len) {
    return mp_pack_call_i32((mp_pack_t *)pack, func, args, nargs, out_result, errbuf, errbuf_len);
}
void *pm_wasmmod_pack_lookup_fn(pm_wasmmod_pack_t *pack, const char *func) {
    return (void *)(uintptr_t)mp_pack_lookup_fn((mp_pack_t *)pack, func);
}

