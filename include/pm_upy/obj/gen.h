/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_GEN_H_
#define PM_PM_UPY_OBJ_GEN_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_gen_available(void);

#include "pm_upy/obj/core.h"
int pm_upy_gen_resume(pm_upy_obj_t gen, pm_upy_obj_t send_val, pm_upy_obj_t *ret_out);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_GEN_H_ */
