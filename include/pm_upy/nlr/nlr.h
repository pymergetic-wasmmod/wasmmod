/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_NLR_NLR_H_
#define PM_PM_UPY_NLR_NLR_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Thin docs + wrappers; real nlr_buf_t stays in py/nlr.h for C callers that need it. */
int pm_upy_nlr_available(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_NLR_NLR_H_ */
