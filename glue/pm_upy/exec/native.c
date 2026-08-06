/*
 * Load a native/dynruntime .mpy image (same path as persistent code load+run).
 */

#include "pm_upy/exec/native.h"
#include "pm_upy/exec/rawcode.h"
#include "pm_common.h"

int pm_upy_dynruntime_load(const uint8_t *mpy, size_t len) {
    return pm_upy_raw_code_load_mem(mpy, len);
}
