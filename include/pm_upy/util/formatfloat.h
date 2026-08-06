/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_UTIL_FORMATFLOAT_H_
#define PM_PM_UPY_UTIL_FORMATFLOAT_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_formatfloat_available(void);

#include <stddef.h>
int pm_upy_format_float(double v, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_UTIL_FORMATFLOAT_H_ */
