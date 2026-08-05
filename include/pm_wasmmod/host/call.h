/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_WASMMOD_HOST_CALL_H_
#define PM_PM_WASMMOD_HOST_CALL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_common.h"

int pm_wasmmod_host_call_export_i32(const char *pack, size_t pack_len,
    const char *func, size_t func_len,
    uint32_t nargs, const int32_t *args, int32_t *out);

/* Guest → host call slots (wasmmod.host). */
#include "pm_guest.h"
#if PM_WASMMOD_GUEST
MP_WASM_IMPORT("wasmmod.host", int32_t, call_i32, int32_t slot, int32_t arg);
MP_WASM_IMPORT("wasmmod.host", int32_t, call0_i32, int32_t slot);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_HOST_CALL_H_ */
