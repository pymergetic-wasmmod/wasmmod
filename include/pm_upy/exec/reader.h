/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_EXEC_READER_H_
#define PM_PM_UPY_EXEC_READER_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_reader_available(void);

#include <stddef.h>
#include <stdint.h>
void *pm_upy_reader_new_mem(const uint8_t *data, size_t len);
void *pm_upy_reader_new_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_EXEC_READER_H_ */
