/*
 * websocket.websocket(sock) wrap when present.
 */

#include "pm_upy/lib/websocket.h"
#include "py/obj.h"
#include "py/runtime.h"

#ifndef MICROPY_PY_WEBSOCKET
#define MICROPY_PY_WEBSOCKET 0
#endif

pm_upy_obj_t pm_upy_websocket_wrap(pm_upy_obj_t sock) {
#if MICROPY_PY_WEBSOCKET
    if (!sock) {
        return (pm_upy_obj_t)(uintptr_t)mp_const_none;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t mod = mp_import_name(MP_QSTR_websocket, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t cls = mp_load_attr(mod, MP_QSTR_websocket);
        mp_obj_t out = mp_call_function_1(cls, (mp_obj_t)(uintptr_t)sock);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)out;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)sock;
    return (pm_upy_obj_t)0;
#endif
}
