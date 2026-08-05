/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_LOOP_REPL_H_
#define PM_PM_UPY_LOOP_REPL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
int pm_upy_repl_start(void);
void pm_upy_repl_stop(void);
int pm_upy_repl_active(void);
int pm_upy_repl_feed_line(const char *line, size_t len);
const char *pm_upy_repl_prompt(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LOOP_REPL_H_ */
