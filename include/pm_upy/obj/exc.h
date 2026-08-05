/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_EXC_H_
#define PM_PM_UPY_OBJ_EXC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_upy/obj/core.h"
void pm_upy_raise_msg(const char *type_name, const char *msg);
void pm_upy_raise_feature(const char *api_name);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_EXC_H_ */
