/*
 * Generator resume.
 */

#include "pm_upy/obj/gen.h"
#include "pm_common.h"
#include "py/objgenerator.h"
#include "py/runtime.h"

int pm_upy_gen_resume(pm_upy_obj_t gen, pm_upy_obj_t send_val, pm_upy_obj_t *ret_out) {
    mp_obj_t ret = MP_OBJ_NULL;
    mp_vm_return_kind_t kind = mp_obj_gen_resume(
        (mp_obj_t)(uintptr_t)gen,
        (mp_obj_t)(uintptr_t)send_val,
        MP_OBJ_NULL,
        &ret);
    if (ret_out) {
        *ret_out = (pm_upy_obj_t)(uintptr_t)(ret == MP_OBJ_NULL ? mp_const_none : ret);
    }
    return (int)kind;
}
