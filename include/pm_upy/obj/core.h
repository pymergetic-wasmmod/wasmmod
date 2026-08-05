/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_OBJ_CORE_H_
#define PM_PM_UPY_OBJ_CORE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
typedef uintptr_t pm_upy_obj_t;
pm_upy_obj_t pm_upy_obj_none(void);
pm_upy_obj_t pm_upy_obj_new_int_from_ll(long long i);
long long pm_upy_obj_get_ll(pm_upy_obj_t o);
pm_upy_obj_t pm_upy_obj_new_str(const char *s, size_t len);
pm_upy_obj_t pm_upy_obj_new_bytes(const uint8_t *b, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_CORE_H_ */
