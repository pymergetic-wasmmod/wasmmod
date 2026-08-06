/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_UTIL_RINGBUF_H_
#define PM_PM_UPY_UTIL_RINGBUF_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_ringbuf_available(void);

#include <stdint.h>
int pm_upy_ringbuf_put(void *rb, uint8_t v);
int pm_upy_ringbuf_get(void *rb, uint8_t *v);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_UTIL_RINGBUF_H_ */
