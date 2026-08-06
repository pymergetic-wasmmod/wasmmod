/*
 * asyncio.run / create_task via the builtin asyncio module when present.
 */

#include "pm_upy/lib/asyncio.h"
#include "pm_common.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_PY_ASYNCIO
#define MICROPY_PY_ASYNCIO 0
#endif

int pm_upy_asyncio_run(pm_upy_obj_t coro) {
#if MICROPY_PY_ASYNCIO
    if (!coro) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = mp_import_name(qstr_from_str("asyncio"), mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t run = mp_load_attr(mod, qstr_from_str("run"));
        mp_call_function_1(run, (mp_obj_t)(uintptr_t)coro);
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)coro;
    return PM_ERR_FEATURE;
#endif
}

pm_upy_obj_t pm_upy_asyncio_create_task(pm_upy_obj_t coro) {
#if MICROPY_PY_ASYNCIO
    if (!coro) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = mp_import_name(qstr_from_str("asyncio"), mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t create_task = mp_load_attr(mod, qstr_from_str("create_task"));
        mp_obj_t out = mp_call_function_1(create_task, (mp_obj_t)(uintptr_t)coro);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)coro;
    return (pm_upy_obj_t)0;
#endif
}
