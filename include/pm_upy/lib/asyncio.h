/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_LIB_ASYNCIO_H_
#define PM_PM_UPY_LIB_ASYNCIO_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_asyncio_available(void);

#include "pm_upy/obj/core.h"
int pm_upy_asyncio_run(pm_upy_obj_t coro);
pm_upy_obj_t pm_upy_asyncio_create_task(pm_upy_obj_t coro);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LIB_ASYNCIO_H_ */
