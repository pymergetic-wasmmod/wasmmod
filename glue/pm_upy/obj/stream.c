/*
 * Stream read/write/seek/close wrappers.
 */

#include "pm_upy/obj/stream.h"
#include "pm_common.h"
#include "py/stream.h"

int pm_upy_stream_rw(pm_upy_obj_t stream, void *buf, size_t len, int write) {
    if (!buf && len) {
        return PM_ERR_ARG;
    }
    int errcode = 0;
    byte flags = write ? MP_STREAM_RW_WRITE : MP_STREAM_RW_READ;
    mp_uint_t n = mp_stream_rw(
        (mp_obj_t)(uintptr_t)stream, buf, (mp_uint_t)len, &errcode, flags);
    if (n == MP_STREAM_ERROR) {
        return errcode ? -errcode : PM_ERR;
    }
    return (int)n;
}

int pm_upy_stream_seek(pm_upy_obj_t stream, int64_t off, int whence) {
    int errcode = 0;
    mp_off_t pos = mp_stream_seek(
        (mp_obj_t)(uintptr_t)stream, (mp_off_t)off, whence, &errcode);
    if (errcode) {
        return -errcode;
    }
    (void)pos;
    return PM_OK;
}

int pm_upy_stream_close(pm_upy_obj_t stream) {
    mp_stream_close((mp_obj_t)(uintptr_t)stream);
    return PM_OK;
}
