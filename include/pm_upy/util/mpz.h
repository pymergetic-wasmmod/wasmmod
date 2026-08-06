/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_UTIL_MPZ_H_
#define PM_PM_UPY_UTIL_MPZ_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "pm_upy/obj/core.h"

int pm_upy_mpz_available(void);
pm_upy_obj_t pm_upy_mpz_from_int(int64_t v);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_UTIL_MPZ_H_ */
