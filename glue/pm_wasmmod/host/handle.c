/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_wasmmod/host/handle.h"
#include "host.h"
#include "py/obj.h"
int32_t pm_wasmmod_handle_register_ptr(void *obj) {
    return mp_wasm_handle_register((mp_obj_t)obj);
}
void *pm_wasmmod_handle_resolve_ptr(int32_t handle) {
    return (void *)mp_wasm_handle_resolve(handle);
}
bool pm_wasmmod_handle_free(int32_t handle) { return mp_wasm_handle_free(handle); }
void pm_wasmmod_handle_clear_all(void) { mp_wasm_handle_clear_all(); }

