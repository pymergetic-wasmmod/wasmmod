/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_upy/vfs/vfs.h"
#include "pm_common.h"

#include <string.h>

#ifndef MICROPY_VFS
#define MICROPY_VFS 0
#endif

#if MICROPY_VFS
#include "extmod/vfs.h"
#include "py/obj.h"
#include "py/runtime.h"
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

int pm_upy_vfs_mount(pm_upy_obj_t vfs, const char *mount_point) {
#if MICROPY_VFS
    if (!mount_point) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t args[2] = {
            (mp_obj_t)(uintptr_t)vfs,
            mp_obj_new_str(mount_point, strlen(mount_point)),
        };
        mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
        nlr_pop();
        return PM_OK;
    }
    return PM_ERR;
#else
    (void)vfs;
    (void)mount_point;
    return PM_ERR_FEATURE;
#endif
}

pm_upy_obj_t pm_upy_vfs_listdir(const char *path) {
#if MICROPY_VFS
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t args[1];
        size_t n = 0;
        if (path && path[0]) {
            args[0] = mp_obj_new_str(path, strlen(path));
            n = 1;
        }
        mp_obj_t lst = mp_vfs_listdir(n, args);
        nlr_pop();
        return (pm_upy_obj_t)(uintptr_t)lst;
    }
    return (pm_upy_obj_t)(uintptr_t)mp_const_none;
#else
    (void)path;
    return (pm_upy_obj_t)0;
#endif
}

int pm_upy_vfs_import_stat(const char *path) {
#if MICROPY_VFS
    if (!path) {
        return PM_ERR_ARG;
    }
    mp_import_stat_t st = mp_vfs_import_stat(path);
    return (int)st;
#else
    (void)path;
    return PM_ERR_FEATURE;
#endif
}

pm_upy_obj_t pm_upy_builtin_open(const char *path, const char *mode) {
    pm_upy_obj_t out = 0;
    if (pm_upy_vfs_open(path, mode, &out) != PM_OK) {
        return (pm_upy_obj_t)0;
    }
    return out;
}
