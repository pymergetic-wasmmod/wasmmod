/*
 * json.loads / json.dumps via the builtin json module when present.
 */

#include "pm_upy/lib/json.h"
#include "pm_common.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_PY_JSON
#define MICROPY_PY_JSON 0
#endif

pm_upy_obj_t pm_upy_json_loads(const char *s) {
#if MICROPY_PY_JSON
    if (!s) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t json = mp_import_name(MP_QSTR_json, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t loads = mp_load_attr(json, MP_QSTR_loads);
        mp_obj_t out = mp_call_function_1(loads, mp_obj_new_str(s, strlen(s)));
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)s;
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}

pm_upy_obj_t pm_upy_json_dumps(pm_upy_obj_t o) {
#if MICROPY_PY_JSON
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t json = mp_import_name(MP_QSTR_json, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t dumps = mp_load_attr(json, MP_QSTR_dumps);
        mp_obj_t out = mp_call_function_1(dumps, (mp_obj_t)(uintptr_t)o);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)o;
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}
