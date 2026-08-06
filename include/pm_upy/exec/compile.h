/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_EXEC_COMPILE_H_
#define PM_PM_UPY_EXEC_COMPILE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_upy/obj/core.h"

/* Lexer/parse/compile faces — fail with PM_ERR_FEATURE when compiler off. */
int pm_upy_compile_available(void);
pm_upy_obj_t pm_upy_compile(const char *src, const char *filename, int kind);
int pm_upy_compile_to_raw_code(const char *src, void **raw_out);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_EXEC_COMPILE_H_ */
