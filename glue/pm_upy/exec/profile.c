/*
 * sys.settrace / instruction-tick hooks when MICROPY_PY_SYS_SETTRACE is on.
 */

#include "pm_upy/exec/profile.h"
#include "pm_common.h"
#include "py/mpconfig.h"
#include "py/runtime.h"

#ifndef MICROPY_PY_SYS_SETTRACE
#define MICROPY_PY_SYS_SETTRACE 0
#endif

#if MICROPY_PY_SYS_SETTRACE
#include "py/profile.h"
#endif

int pm_upy_profile_settrace(void *cb) {
#if MICROPY_PY_SYS_SETTRACE
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t obj = cb ? (mp_obj_t)(uintptr_t)cb : mp_const_none;
        mp_prof_settrace(obj);
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)cb;
    return PM_ERR_FEATURE;
#endif
}

void pm_upy_prof_instr_tick(void) {
#if MICROPY_PY_SYS_SETTRACE
    /* Tick is driven from the VM with a code_state; no-op without one. */
#else
#endif
}
