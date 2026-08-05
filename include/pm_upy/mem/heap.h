/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_MEM_HEAP_H_
#define PM_PM_UPY_MEM_HEAP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
void *pm_upy_alloc(size_t size);
void pm_upy_free(void *ptr);
void *pm_upy_realloc(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_MEM_HEAP_H_ */
