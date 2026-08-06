/*
 * Leftover stubs for APIs without a stable host wrap in this wave.
 * (Most former entries moved to dedicated glue TUs.)
 */

#include "pm_common.h"

#include <stddef.h>
#include <stdint.h>

#include "py/mphal.h"

uint32_t pm_upy_sleep_us(uint64_t us) {
    while (us > 1000000ull) {
        mp_hal_delay_us(1000000u);
        us -= 1000000ull;
    }
    if (us) {
        mp_hal_delay_us((uint32_t)us);
    }
    return 0;
}
