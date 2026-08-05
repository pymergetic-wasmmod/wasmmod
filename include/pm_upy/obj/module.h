/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_MODULE_H_
#define PM_PM_UPY_OBJ_MODULE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_upy/obj/core.h"
#include <stdbool.h>

/* Module / bind face — wasmmod installs via pm_wasmmod_module_install. */
pm_upy_obj_t pm_upy_import_name(const char *name);
int pm_upy_bind_reg(const char *full_module, const char *func, void *fn_ptr);
int pm_upy_module_install_face(const char *full_name, void *face);
bool pm_upy_module_has(const char *full_name);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_MODULE_H_ */
