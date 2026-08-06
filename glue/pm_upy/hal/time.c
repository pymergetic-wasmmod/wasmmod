/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


#include "pm_upy/hal/time.h"
#include "pm_common.h"
#include "py/mphal.h"
#include "shared/timeutils/timeutils.h"

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

uint32_t pm_upy_ticks_cpu(void) {
#ifdef mp_hal_ticks_cpu
    return mp_hal_ticks_cpu();
#else
    return mp_hal_ticks_us();
#endif
}

int pm_upy_time_localtime(int64_t seconds, int32_t out[8]) {
    if (!out) {
        return PM_ERR_ARG;
    }
    timeutils_struct_time_t tm;
    timeutils_seconds_since_epoch_to_struct_time((mp_uint_t)seconds, &tm);
    out[0] = (int32_t)tm.tm_year;
    out[1] = (int32_t)tm.tm_mon;
    out[2] = (int32_t)tm.tm_mday;
    out[3] = (int32_t)tm.tm_hour;
    out[4] = (int32_t)tm.tm_min;
    out[5] = (int32_t)tm.tm_sec;
    out[6] = (int32_t)tm.tm_wday;
    out[7] = (int32_t)tm.tm_yday;
    return PM_OK;
}

int64_t pm_upy_time_mktime(const int32_t tm[8]) {
    if (!tm) {
        return 0;
    }
    return (int64_t)timeutils_mktime(
        (mp_uint_t)tm[0], (mp_uint_t)tm[1], (mp_uint_t)tm[2],
        (mp_uint_t)tm[3], (mp_uint_t)tm[4], (mp_uint_t)tm[5]);
}

