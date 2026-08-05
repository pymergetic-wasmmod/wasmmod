/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/vfs/vfs.h"
#include "pm_common.h"

#ifndef MICROPY_VFS
#define MICROPY_VFS 0
#endif

#if MICROPY_VFS
#include "extmod/vfs.h"
#endif

int pm_upy_vfs_open(const char *path, const char *mode, pm_upy_obj_t *out) {
#if MICROPY_VFS
    if (!path || !out) {
        return PM_ERR_ARG;
    }
    if (!mode) {
        mode = "r";
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t args[2] = {
            mp_obj_new_str(path, strlen(path)),
            mp_obj_new_str(mode, strlen(mode)),
        };
        mp_obj_t f = mp_vfs_open(2, args, (mp_map_t *)&mp_const_empty_map);
        nlr_pop();
        *out = (pm_upy_obj_t)(uintptr_t)f;
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)path;
    (void)mode;
    (void)out;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_vfs_stat(const char *path, pm_upy_obj_t *out) {
#if MICROPY_VFS
    if (!path || !out) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t st = mp_vfs_stat(mp_obj_new_str(path, strlen(path)));
        nlr_pop();
        *out = (pm_upy_obj_t)(uintptr_t)st;
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)path;
    (void)out;
    return PM_ERR_FEATURE;
#endif
}
