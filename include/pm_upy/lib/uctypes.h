/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_LIB_UCTYPES_H_
#define PM_PM_UPY_LIB_UCTYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_uctypes_available(void);

#include <stdint.h>
#include "pm_upy/obj/core.h"
pm_upy_obj_t pm_upy_uctypes_struct(uint32_t addr, pm_upy_obj_t desc, int flags);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LIB_UCTYPES_H_ */
