/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_STREAM_H_
#define PM_PM_UPY_OBJ_STREAM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include "pm_upy/obj/core.h"
int pm_upy_stream_available(void);

#include <stdint.h>
int pm_upy_stream_rw(pm_upy_obj_t stream, void *buf, size_t len, int write);
int pm_upy_stream_seek(pm_upy_obj_t stream, int64_t off, int whence);
int pm_upy_stream_close(pm_upy_obj_t stream);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_STREAM_H_ */
