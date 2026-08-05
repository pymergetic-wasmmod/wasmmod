/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_WASMMOD_HOST_COOKIE_H_
#define PM_PM_WASMMOD_HOST_COOKIE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_common.h"

int32_t pm_wasmmod_mem_alloc(uint32_t size);
int32_t pm_wasmmod_mem_alloc_copy(const uint8_t *data, uint32_t len);
bool pm_wasmmod_mem_free(int32_t cookie);
void pm_wasmmod_mem_clear_all(void);
bool pm_wasmmod_mem_valid(int32_t cookie);
uint32_t pm_wasmmod_mem_len(int32_t cookie);
const uint8_t *pm_wasmmod_mem_data(int32_t cookie, uint32_t *len_out);
bool pm_wasmmod_mem_set(int32_t cookie, const uint8_t *data, uint32_t len);

/* Guest → host mem cookies (wasmmod.host). */
#include "pm_guest.h"
#if PM_WASMMOD_GUEST
MP_WASM_IMPORT("wasmmod.host", int32_t, mem_alloc, int32_t size);
MP_WASM_IMPORT("wasmmod.host", void, mem_free, int32_t cookie);
MP_WASM_IMPORT("wasmmod.host", int32_t, mem_len, int32_t cookie);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_HOST_COOKIE_H_ */
