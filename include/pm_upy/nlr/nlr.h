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

/*
 * push/pop/jump must be macros in the caller's TU (setjmp). When py/nlr.h is
 * visible, these alias the MicroPython NLR API; otherwise use py/nlr.h directly.
 */
#if defined(__has_include)
#if __has_include("py/nlr.h")
#include "py/nlr.h"
#define pm_upy_nlr_push(buf) nlr_push(buf)
#define pm_upy_nlr_pop() nlr_pop()
#define pm_upy_nlr_jump(val) nlr_jump(val)
#define pm_upy_nlr_jump_fail(val) nlr_jump_fail(val)
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_NLR_NLR_H_ */
