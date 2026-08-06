/*
 * ssl.wrap_socket via the builtin ssl module when present.
 */

#include "pm_upy/lib/ssl.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_PY_SSL
#define MICROPY_PY_SSL 0
#endif

pm_upy_obj_t pm_upy_ssl_wrap_socket(pm_upy_obj_t sock) {
#if MICROPY_PY_SSL
    if (!sock) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = mp_import_name(qstr_from_str("ssl"), mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t wrap = mp_load_attr(mod, MP_QSTR_wrap_socket);
        mp_obj_t out = mp_call_function_1(wrap, (mp_obj_t)(uintptr_t)sock);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)sock;
    return (pm_upy_obj_t)0;
#endif
}
