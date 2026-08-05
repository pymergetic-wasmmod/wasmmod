/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_EXEC_PYEXEC_H_
#define PM_PM_UPY_EXEC_PYEXEC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
int pm_upy_pyexec_file(const char *path);
int pm_upy_pyexec_vstr(const char *src, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_EXEC_PYEXEC_H_ */
