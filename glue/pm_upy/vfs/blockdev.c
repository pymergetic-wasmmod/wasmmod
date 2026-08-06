/*
 * VFS block device helpers.
 */

#include "pm_upy/vfs/blockdev.h"
#include "pm_common.h"
#include "py/runtime.h"

#ifndef MICROPY_VFS
#define MICROPY_VFS 0
#endif

#if MICROPY_VFS
#include "extmod/vfs.h"
#endif

int pm_upy_vfs_blockdev_read(pm_upy_obj_t bdev, uint8_t *buf, uint32_t block_num) {
#if MICROPY_VFS
    if (!bdev || !buf) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_vfs_blockdev_t bd;
        mp_vfs_blockdev_init(&bd, (mp_obj_t)(uintptr_t)bdev);
        int ret = mp_vfs_blockdev_read(&bd, block_num, 1, buf);
        nlr_pop();
        return ret == 0 ? PM_OK : PM_ERR;
    }
    return PM_ERR;
#else
    (void)bdev;
    (void)buf;
    (void)block_num;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_vfs_blockdev_write(pm_upy_obj_t bdev, const uint8_t *buf, uint32_t block_num) {
#if MICROPY_VFS
    if (!bdev || !buf) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_vfs_blockdev_t bd;
        mp_vfs_blockdev_init(&bd, (mp_obj_t)(uintptr_t)bdev);
        int ret = mp_vfs_blockdev_write(&bd, block_num, 1, buf);
        nlr_pop();
        return ret == 0 ? PM_OK : PM_ERR;
    }
    return PM_ERR;
#else
    (void)bdev;
    (void)buf;
    (void)block_num;
    return PM_ERR_FEATURE;
#endif
}

int pm_upy_vfs_blockdev_ioctl(pm_upy_obj_t bdev, uint32_t op, uint32_t arg) {
#if MICROPY_VFS
    if (!bdev) {
        return PM_ERR_ARG;
    }
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_vfs_blockdev_t bd;
        mp_vfs_blockdev_init(&bd, (mp_obj_t)(uintptr_t)bdev);
        mp_obj_t ret = mp_vfs_blockdev_ioctl(&bd, op, arg);
        int v = (int)mp_obj_get_int(ret);
        nlr_pop();
        return v;
    }
    return PM_ERR;
#else
    (void)bdev;
    (void)op;
    (void)arg;
    return PM_ERR_FEATURE;
#endif
}
