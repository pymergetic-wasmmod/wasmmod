/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_VFS_BLOCKDEV_H_
#define PM_PM_UPY_VFS_BLOCKDEV_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_vfs_blockdev_available(void);

#include <stdint.h>
#include "pm_upy/obj/core.h"
int pm_upy_vfs_blockdev_read(pm_upy_obj_t bdev, uint8_t *buf, uint32_t block_num);
int pm_upy_vfs_blockdev_write(pm_upy_obj_t bdev, const uint8_t *buf, uint32_t block_num);
int pm_upy_vfs_blockdev_ioctl(pm_upy_obj_t bdev, uint32_t op, uint32_t arg);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_VFS_BLOCKDEV_H_ */
