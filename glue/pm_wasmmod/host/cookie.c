/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_wasmmod/host/cookie.h"
#include "host.h"
int32_t pm_wasmmod_mem_alloc(uint32_t size) { return mp_wasm_mem_alloc(size); }
int32_t pm_wasmmod_mem_alloc_copy(const uint8_t *data, uint32_t len) { return mp_wasm_mem_alloc_copy(data, len); }
bool pm_wasmmod_mem_free(int32_t cookie) { return mp_wasm_mem_free(cookie); }
void pm_wasmmod_mem_clear_all(void) { mp_wasm_mem_clear_all(); }
bool pm_wasmmod_mem_valid(int32_t cookie) { return mp_wasm_mem_valid(cookie); }
uint32_t pm_wasmmod_mem_len(int32_t cookie) { return mp_wasm_mem_len(cookie); }
const uint8_t *pm_wasmmod_mem_data(int32_t cookie, uint32_t *len_out) { return mp_wasm_mem_data(cookie, len_out); }
bool pm_wasmmod_mem_set(int32_t cookie, const uint8_t *data, uint32_t len) { return mp_wasm_mem_set(cookie, data, len); }

