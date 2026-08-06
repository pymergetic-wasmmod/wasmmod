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

pm_upy_obj_t pm_upy_obj_new_int(intptr_t i);
intptr_t pm_upy_obj_get_int(pm_upy_obj_t o);
int pm_upy_obj_str_get(pm_upy_obj_t o, const char **data_out, size_t *len_out);
pm_upy_obj_t pm_upy_obj_new_bool(int v);
pm_upy_obj_t pm_upy_obj_new_float(double v);
double pm_upy_obj_get_float(pm_upy_obj_t o);
pm_upy_obj_t pm_upy_obj_new_bytearray(size_t n, const uint8_t *data);
pm_upy_obj_t pm_upy_obj_new_memoryview(pm_upy_obj_t base);
pm_upy_obj_t pm_upy_obj_new_slice(pm_upy_obj_t start, pm_upy_obj_t stop, pm_upy_obj_t step);
pm_upy_obj_t pm_upy_obj_new_complex(double re, double im);
pm_upy_obj_t pm_upy_obj_new_set(size_t n);
pm_upy_obj_t pm_upy_obj_to_str(pm_upy_obj_t o);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_OBJ_CORE_H_ */
