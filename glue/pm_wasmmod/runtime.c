/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_wasmmod/runtime.h"
#include "runtime.h"
bool pm_wasmmod_runtime_init(void) { return mp_wasm_runtime_init(); }
void pm_wasmmod_runtime_deinit(void) { mp_wasm_runtime_deinit(); }

