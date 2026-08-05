/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_BUF_H_
#define PM_PM_UPY_OBJ_BUF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_upy/obj/core.h"
int pm_upy_buf_get(pm_upy_obj_t o, const uint8_t **ptr, size_t *len);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_BUF_H_ */
