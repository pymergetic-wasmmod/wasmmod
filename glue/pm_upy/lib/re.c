/*
 * re.compile / re.match via the builtin re module when present.
 */

#include "pm_upy/lib/re.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_PY_RE
#define MICROPY_PY_RE 0
#endif

pm_upy_obj_t pm_upy_re_compile(const char *pat) {
#if MICROPY_PY_RE
    if (!pat) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t re = mp_import_name(MP_QSTR_re, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t compile = mp_load_attr(re, MP_QSTR_compile);
        mp_obj_t out = mp_call_function_1(compile, mp_obj_new_str(pat, strlen(pat)));
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)pat;
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}

pm_upy_obj_t pm_upy_re_match(pm_upy_obj_t regex, const char *s) {
#if MICROPY_PY_RE
    if (!s) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t match;
        if (regex) {
            match = mp_load_attr((mp_obj_t)(uintptr_t)regex, MP_QSTR_match);
            match = mp_call_function_1(match, mp_obj_new_str(s, strlen(s)));
        } else {
            mp_obj_t re = mp_import_name(MP_QSTR_re, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
            mp_obj_t fn = mp_load_attr(re, MP_QSTR_match);
            match = mp_call_function_1(fn, mp_obj_new_str(s, strlen(s)));
        }
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)match;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)regex;
    (void)s;
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#endif
}
