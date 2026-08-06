/*
 * bluetooth.BLE() when MICROPY_PY_BLUETOOTH is built.
 */

#include "pm_upy/lib/bluetooth.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_PY_BLUETOOTH
#define MICROPY_PY_BLUETOOTH 0
#endif

pm_upy_obj_t pm_upy_bluetooth_init(void) {
#if MICROPY_PY_BLUETOOTH
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = mp_import_name(qstr_from_str("bluetooth"), mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t ble = mp_load_attr(mod, qstr_from_str("BLE"));
        mp_obj_t out = mp_call_function_0(ble);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    return (pm_upy_obj_t)0;
#endif
}
