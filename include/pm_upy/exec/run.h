/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_EXEC_RUN_H_
#define PM_PM_UPY_EXEC_RUN_H_

#ifdef __cplusplus
extern "C" {
#endif

int pm_upy_run_str(const char *src);
int pm_upy_run_script(const char *path);
int pm_upy_parse_compile_execute(const char *src);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_EXEC_RUN_H_ */
