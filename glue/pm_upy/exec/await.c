/*
 * Awaitable resume via host object handles + mp_resume.
 */

#include "pm_upy/exec/await.h"
#include "pm_common.h"
#include "host.h"
#include "py/runtime.h"

uint32_t pm_upy_await(uint32_t self_h, uint32_t child_h) {
    (void)self_h;
    mp_obj_t child = mp_wasm_handle_resolve((int32_t)child_h);
    if (child == mp_const_none) {
        return 0;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t ret = MP_OBJ_NULL;
        mp_vm_return_kind_t kind = mp_resume(child, mp_const_none, MP_OBJ_NULL, &ret);
        nlr_pop();
        if (kind == MP_VM_RETURN_EXCEPTION) {
            return 0;
        }
        if (ret == MP_OBJ_NULL) {
            ret = mp_const_none;
        }
        return (uint32_t)mp_wasm_handle_register(ret);
    }
    return 0;
}

uint32_t pm_upy_new_awaitable(uint32_t handle) {
    /* Handle already names a Python awaitable/generator; pass through. */
    return handle;
}

int pm_upy_resume(void *obj) {
    if (!obj) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t ret = MP_OBJ_NULL;
        mp_vm_return_kind_t kind = mp_resume((mp_obj_t)(uintptr_t)obj, mp_const_none, MP_OBJ_NULL, &ret);
        nlr_pop();
        return (int)kind;
    }
    return PM_ERR;
}
