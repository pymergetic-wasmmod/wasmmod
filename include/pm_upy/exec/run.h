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

#include <stddef.h>
#include <stdint.h>
#include "pm_upy/obj/core.h"

int pm_upy_run_str(const char *src);
int pm_upy_run_script(const char *path);
int pm_upy_parse_compile_execute(const char *src);
int pm_upy_execute_bytecode(const uint8_t *bc, size_t len);
pm_upy_obj_t pm_upy_make_function(void *raw_code);
pm_upy_obj_t pm_upy_make_closure(void *raw_code, size_t n_closed, pm_upy_obj_t *closed);

/* Guest Wasm: run_str(off, len) over linear memory; ELF: plain C string. */
#include "pm_guest.h"
#if PM_WASMMOD_GUEST
#if PM_WASMMOD_GUEST_WASM
MP_WASM_IMPORT("micropython.runtime", int, run_str, int32_t off, int32_t len);
#else
MP_WASM_IMPORT("micropython.runtime", int, run_str, const char *src);
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_EXEC_RUN_H_ */
