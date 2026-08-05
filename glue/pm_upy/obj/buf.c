/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/obj/buf.h"
#include "pm_common.h"
#include "py/obj.h"

int pm_upy_buf_get(pm_upy_obj_t o, const uint8_t **ptr, size_t *len) {
    mp_buffer_info_t bufinfo;
    if (!mp_get_buffer((mp_obj_t)(uintptr_t)o, &bufinfo, MP_BUFFER_READ)) {
        return PM_ERR;
    }
    if (ptr) {
        *ptr = bufinfo.buf;
    }
    if (len) {
        *len = bufinfo.len;
    }
    return PM_OK;
}
