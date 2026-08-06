/*
 * uctypes.struct via the builtin uctypes module when present.
 */

#include "pm_upy/lib/uctypes.h"
#include "py/obj.h"
#include "py/runtime.h"

#ifndef MICROPY_PY_UCTYPES
#define MICROPY_PY_UCTYPES 0
#endif

pm_upy_obj_t pm_upy_uctypes_struct(uint32_t addr, pm_upy_obj_t desc, int flags) {
#if MICROPY_PY_UCTYPES
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = mp_import_name(MP_QSTR_uctypes, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t cls = mp_load_attr(mod, MP_QSTR_struct);
        mp_obj_t args[3] = {
            mp_obj_new_int_from_uint(addr),
            (mp_obj_t)(uintptr_t)desc,
            MP_OBJ_NEW_SMALL_INT(flags),
        };
        mp_obj_t out = mp_call_function_n_kw(cls, 3, 0, args);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)addr;
    (void)desc;
    (void)flags;
    return (pm_upy_obj_t)0;
#endif
}
