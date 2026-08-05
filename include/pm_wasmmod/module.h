/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_WASMMOD_MODULE_H_
#define PM_PM_WASMMOD_MODULE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_common.h"

/* Gut-exposure: register wasmmod as a named module face (bind/reg style).
 * full_name e.g. "pymergetic.wasmmod" or "wasm".
 */
bool pm_wasmmod_module_install(const char *full_name);
bool pm_wasmmod_module_installed(void);
const char *pm_wasmmod_module_name(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_MODULE_H_ */
