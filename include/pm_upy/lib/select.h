/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_LIB_SELECT_H_
#define PM_PM_UPY_LIB_SELECT_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_select_available(void);

#include "pm_upy/obj/core.h"
pm_upy_obj_t pm_upy_select_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LIB_SELECT_H_ */
