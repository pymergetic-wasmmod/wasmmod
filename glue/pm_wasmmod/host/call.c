/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_wasmmod/host/call.h"
#include "host.h"
int pm_wasmmod_host_call_export_i32(const char *pack, size_t pack_len,
    const char *func, size_t func_len,
    uint32_t nargs, const int32_t *args, int32_t *out) {
    return mp_wasm_host_call_export_i32(pack, pack_len, func, func_len, nargs, args, out);
}

