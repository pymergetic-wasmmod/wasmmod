/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_COMMON_H_
#define PM_PM_COMMON_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shared failure codes for pm_wasmmod_* / pm_upy_* (C). */
typedef enum {
    PM_OK = 0,
    PM_ERR = -1,
    PM_ERR_FEATURE = -2,   /* config/menuconfig feature unavailable */
    PM_ERR_ARG = -3,
    PM_ERR_NOMEM = -4,
    PM_ERR_NOT_READY = -5,
} pm_status_t;

#ifdef __cplusplus
}
#endif


#endif /* PM_PM_COMMON_H_ */
