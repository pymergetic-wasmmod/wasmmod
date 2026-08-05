/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_WASMMOD_PATH_FETCH_H_
#define PM_PM_WASMMOD_PATH_FETCH_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_common.h"

bool pm_wasmmod_fetch(const char *url, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_PATH_FETCH_H_ */
