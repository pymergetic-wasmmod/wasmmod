/*
 * Make function/closure from proto/raw code; locate frozen modules.
 */

#include "pm_upy/exec/run.h"
#include "pm_common.h"
#include "py/bc.h"
#include "py/emitglue.h"
#include "py/frozenmod.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <string.h>

pm_upy_obj_t pm_upy_make_function(void *raw_code) {
    if (!raw_code) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_module_context_t *ctx = m_new_obj(mp_module_context_t);
        ctx->module.globals = mp_globals_get();
        memset(&ctx->constants, 0, sizeof(ctx->constants));
        mp_obj_t fun = mp_make_function_from_proto_fun((mp_proto_fun_t)raw_code, ctx, NULL);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)fun;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
}

pm_upy_obj_t pm_upy_make_closure(void *raw_code, size_t n_closed, pm_upy_obj_t *closed) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t fun = (mp_obj_t)(uintptr_t)pm_upy_make_function(raw_code);
        mp_obj_t out = mp_obj_new_closure(fun, n_closed, (const mp_obj_t *)closed);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
}

int pm_upy_find_frozen(const char *name, void **out) {
#if MICROPY_MODULE_FROZEN
    if (!name) {
        return PM_ERR_ARG;
    }
    int frozen_type = 0;
    void *data = NULL;
    mp_import_stat_t st = mp_find_frozen_module(name, &frozen_type, &data);
    if (st == MP_IMPORT_STAT_FILE || st == MP_IMPORT_STAT_DIR) {
        if (out) {
            *out = data;
        }
        return PM_OK;
    }
    if (out) {
        *out = NULL;
    }
    return PM_ERR;
#else
    (void)name;
    if (out) {
        *out = NULL;
    }
    return PM_ERR_FEATURE;
#endif
}
