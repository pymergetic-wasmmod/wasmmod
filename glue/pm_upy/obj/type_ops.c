/*
 * Bound method / closure / generator / cell constructors.
 */

#include "pm_upy/obj/type.h"
#include "py/obj.h"
#include "py/runtime.h"

#ifndef MICROPY_PY_FUNCTION_ATTRS
#define MICROPY_PY_FUNCTION_ATTRS 0
#endif

pm_upy_obj_t pm_upy_obj_new_bound_meth(pm_upy_obj_t meth, pm_upy_obj_t self) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_bound_meth(
        (mp_obj_t)(uintptr_t)meth, (mp_obj_t)(uintptr_t)self);
}

pm_upy_obj_t pm_upy_obj_new_closure(pm_upy_obj_t fun, size_t n, pm_upy_obj_t *closed) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_closure(
        (mp_obj_t)(uintptr_t)fun, n, (const mp_obj_t *)closed);
}

pm_upy_obj_t pm_upy_obj_new_gen_wrap(pm_upy_obj_t fun) {
    /* Generator wrap is bytecode-internal; expose as identity for callable fun. */
    return fun;
}

pm_upy_obj_t pm_upy_obj_new_cell(pm_upy_obj_t obj) {
    return (pm_upy_obj_t)(uintptr_t)mp_obj_new_cell((mp_obj_t)(uintptr_t)obj);
}
