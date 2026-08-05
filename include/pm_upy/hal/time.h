/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_HAL_TIME_H_
#define PM_PM_UPY_HAL_TIME_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
uint32_t pm_upy_ticks_ms(void);
uint32_t pm_upy_ticks_us(void);
uint64_t pm_upy_time_ns(void);
void pm_upy_delay_ms(uint32_t ms);
void pm_upy_delay_us(uint32_t us);

/* Guest: micropython.runtime.ticks_ms (Wasm import attrs / ELF plain extern). */
#include "pm_guest.h"
#if PM_WASMMOD_GUEST
MP_WASM_IMPORT("micropython.runtime", uint32_t, ticks_ms, void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_HAL_TIME_H_ */
