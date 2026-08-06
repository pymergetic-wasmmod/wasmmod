/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_LIB_DEFLATE_H_
#define PM_PM_UPY_LIB_DEFLATE_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_deflate_available(void);

#include <stddef.h>
#include <stdint.h>
int pm_upy_deflate_decompress(const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LIB_DEFLATE_H_ */
