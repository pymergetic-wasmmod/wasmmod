/*
 * Save a function to persistent bytes when SAVE_FUN is enabled.
 */

#include "pm_upy/util/persistentcode.h"
#include "pm_common.h"
#include "py/mpconfig.h"

#ifndef MICROPY_PERSISTENT_CODE_SAVE_FUN
#define MICROPY_PERSISTENT_CODE_SAVE_FUN 0
#endif

#if MICROPY_PERSISTENT_CODE_SAVE_FUN
#include "py/bc.h"
#include "py/obj.h"
#include "py/objfun.h"
#include "py/persistentcode.h"
#include "py/runtime.h"
#endif

int pm_upy_persistentcode_save_fun(pm_upy_obj_t fun, void *writer) {
#if MICROPY_PERSISTENT_CODE_SAVE_FUN
    (void)writer;
    if (!fun) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fun_obj = (mp_obj_t)(uintptr_t)fun;
        if (!mp_obj_is_type(fun_obj, &mp_type_fun_bc)) {
            nlr_pop();
            return PM_ERR_ARG;
        }
        mp_obj_fun_bc_t *f = MP_OBJ_TO_PTR(fun_obj);
        /* Bytecode pointer is a valid proto_fun for bytecode kinds. */
        mp_obj_t bytes = mp_raw_code_save_fun_to_bytes(&f->context->constants, (mp_proto_fun_t)f->bytecode);
        (void)bytes;
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)fun;
    (void)writer;
    return PM_ERR_FEATURE;
#endif
}
