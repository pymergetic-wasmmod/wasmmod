/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_VFS_VFS_H_
#define PM_PM_UPY_VFS_VFS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_upy/obj/core.h"

/** Open path; on success *out is a file-like object. mode defaults to "r" if NULL. */
int pm_upy_vfs_open(const char *path, const char *mode, pm_upy_obj_t *out);

/** Stat path; on success *out is the stat tuple/obj from VFS. */
int pm_upy_vfs_stat(const char *path, pm_upy_obj_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_VFS_VFS_H_ */
