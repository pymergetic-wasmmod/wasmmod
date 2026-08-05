/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_HAL_STDIO_H_
#define PM_PM_UPY_HAL_STDIO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
int pm_upy_stdin_rx(void);
void pm_upy_stdout_tx(const char *s, size_t len);
int pm_upy_stdio_poll(uintptr_t poll);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_HAL_STDIO_H_ */
