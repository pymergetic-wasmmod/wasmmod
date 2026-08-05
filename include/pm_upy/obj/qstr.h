/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_QSTR_H_
#define PM_PM_UPY_OBJ_QSTR_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
uint32_t pm_upy_qstr_from_str(const char *s);
const char *pm_upy_qstr_str(uint32_t q);
size_t pm_upy_qstr_len(uint32_t q);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_QSTR_H_ */
