/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_ARG_H_
#define PM_PM_UPY_OBJ_ARG_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_arg_available(void);

#include <stddef.h>
#include "pm_upy/obj/core.h"
int pm_upy_arg_parse(size_t n_args, const pm_upy_obj_t *args, void *spec);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_ARG_H_ */
