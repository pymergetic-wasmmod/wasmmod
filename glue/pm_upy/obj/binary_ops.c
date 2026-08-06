/*
 * struct-like binary get/set.
 */

#include "pm_upy/obj/binary.h"
#include "pm_common.h"
#include "py/binary.h"

int pm_upy_binary_get(int typecode, const void *p, int64_t *out) {
    if (!p || !out) {
        return PM_ERR_ARG;
    }
    char tc = (char)typecode;
    size_t sz = mp_binary_get_size('@', tc, NULL);
    if (sz == 0) {
        return PM_ERR_ARG;
    }
    bool is_signed = (tc == 'b' || tc == 'h' || tc == 'i' || tc == 'l' || tc == 'q');
    *out = (int64_t)mp_binary_get_int(sz, is_signed, false, (const byte *)p);
    return PM_OK;
}

int pm_upy_binary_set(int typecode, void *p, int64_t val) {
    if (!p) {
        return PM_ERR_ARG;
    }
    char tc = (char)typecode;
    size_t sz = mp_binary_get_size('@', tc, NULL);
    if (sz == 0) {
        return PM_ERR_ARG;
    }
    mp_binary_set_int(sz, (byte *)p, sz, (mp_uint_t)val, false);
    return PM_OK;
}
