/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_upy/hal/time.h"
#include "py/mphal.h"
uint32_t pm_upy_ticks_ms(void) { return mp_hal_ticks_ms(); }
uint32_t pm_upy_ticks_us(void) {
#ifdef mp_hal_ticks_us
    return mp_hal_ticks_us();
#else
    return mp_hal_ticks_ms() * 1000u;
#endif
}
uint64_t pm_upy_time_ns(void) { return (uint64_t)pm_upy_ticks_us() * 1000ull; }
void pm_upy_delay_ms(uint32_t ms) { mp_hal_delay_ms(ms); }
void pm_upy_delay_us(uint32_t us) { mp_hal_delay_us(us); }

