/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_EXEC_RAWCODE_H_
#define PM_PM_UPY_EXEC_RAWCODE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
int pm_upy_raw_code_load_mem(const uint8_t *data, size_t len);
int pm_upy_raw_code_save(void); /* stub */

int pm_upy_raw_code_load_file(const char *path, void **raw_out);
int pm_upy_find_frozen(const char *name, void **out);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_EXEC_RAWCODE_H_ */
