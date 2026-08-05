/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_MEM_GC_H_
#define PM_PM_UPY_MEM_GC_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_gc_enabled(void);
int pm_upy_gc_collect(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_MEM_GC_H_ */
