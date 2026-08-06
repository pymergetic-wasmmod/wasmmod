/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_LOOP_STEP_H_
#define PM_PM_UPY_LOOP_STEP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
int pm_upy_loop_step(void);
int pm_upy_loop_feed(const uint8_t *ptr, size_t len);
int pm_upy_loop_reset(void);
int pm_upy_handle_pending(void);

#include "pm_guest.h"
#if PM_WASMMOD_GUEST
MP_WASM_IMPORT("micropython.runtime", int, handle_pending, void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LOOP_STEP_H_ */
