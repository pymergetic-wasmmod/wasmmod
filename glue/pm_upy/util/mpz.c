/*
 * Multi-precision int from int64 (mpz or longlong backend).
 */

#include "pm_upy/util/mpz.h"
#include "py/obj.h"
#include "py/runtime.h"

pm_upy_obj_t pm_upy_mpz_from_int(int64_t v) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t o = mp_obj_new_int_from_ll((long long)v);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)o;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
}
