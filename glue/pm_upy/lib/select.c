/*
 * select.poll() factory via the builtin select module.
 */

#include "pm_upy/lib/select.h"
#include "py/obj.h"
#include "py/runtime.h"

#ifndef MICROPY_PY_SELECT
#define MICROPY_PY_SELECT 0
#endif

pm_upy_obj_t pm_upy_select_poll(void) {
#if MICROPY_PY_SELECT
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t sel = mp_import_name(MP_QSTR_select, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t poll = mp_load_attr(sel, MP_QSTR_poll);
        mp_obj_t out = mp_call_function_0(poll);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}
