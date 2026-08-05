/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_WASMMOD_HOST_HANDLE_H_
#define PM_PM_WASMMOD_HOST_HANDLE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_common.h"

/* Opaque GC-rooted Python object handle (0 invalid). Native hosts may no-op fail. */
int32_t pm_wasmmod_handle_register_ptr(void *obj);
void *pm_wasmmod_handle_resolve_ptr(int32_t handle);
bool pm_wasmmod_handle_free(int32_t handle);
void pm_wasmmod_handle_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_HOST_HANDLE_H_ */
