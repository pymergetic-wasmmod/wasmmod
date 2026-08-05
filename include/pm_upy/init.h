/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_INIT_H_
#define PM_PM_UPY_INIT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
int pm_upy_init(void *heap_start, size_t heap_len);
void pm_upy_deinit(void);
int pm_upy_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_INIT_H_ */
