/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * Leftover stubs for APIs without a stable host wrap in this wave.
 */

#include "pm_common.h"
#include <stdint.h>
#include <stddef.h>

int pm_upy_raw_code_load_mem(const uint8_t *data, size_t len) {
    (void)data;
    (void)len;
    return PM_ERR_FEATURE;
}

int pm_upy_raw_code_save(void) {
    return PM_ERR_FEATURE;
}

uint32_t pm_upy_await(uint32_t a, uint32_t b) {
    (void)a;
    (void)b;
    return 0;
}

uint32_t pm_upy_sleep_us(uint64_t us) {
    (void)us;
    return 0;
}

uint32_t pm_upy_new_awaitable(uint32_t h) {
    (void)h;
    return 0;
}

int pm_upy_resume(void *obj) {
    (void)obj;
    return PM_ERR_FEATURE;
}

int pm_upy_profile_settrace(void *cb) {
    (void)cb;
    return PM_ERR_FEATURE;
}
