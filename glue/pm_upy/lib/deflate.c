/*
 * Deflate decompress via deflate.DeflateIO + io.BytesIO when present.
 */

#include "pm_upy/lib/deflate.h"
#include "pm_common.h"
#include "py/obj.h"
#include "py/runtime.h"

#include <string.h>

#ifndef MICROPY_PY_DEFLATE
#define MICROPY_PY_DEFLATE 0
#endif

int pm_upy_deflate_decompress(const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len) {
#if MICROPY_PY_DEFLATE
    if (!in || !out || !out_len) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t io = mp_import_name(MP_QSTR_io, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t BytesIO = mp_load_attr(io, MP_QSTR_BytesIO);
        mp_obj_t bio = mp_call_function_1(BytesIO, mp_obj_new_bytes(in, in_len));
        mp_obj_t deflate = mp_import_name(MP_QSTR_deflate, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
        mp_obj_t DeflateIO = mp_load_attr(deflate, MP_QSTR_DeflateIO);
        mp_obj_t stream = mp_call_function_1(DeflateIO, bio);
        mp_obj_t read = mp_load_attr(stream, MP_QSTR_read);
        mp_obj_t data = mp_call_function_0(read);
        mp_buffer_info_t info;
        mp_get_buffer_raise(data, &info, MP_BUFFER_READ);
        if (info.len > *out_len) {
            nlr_pop();
            return PM_ERR_NOMEM;
        }
        memcpy(out, info.buf, info.len);
        *out_len = info.len;
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)in;
    (void)in_len;
    (void)out;
    (void)out_len;
    return PM_ERR_FEATURE;
#endif
}
