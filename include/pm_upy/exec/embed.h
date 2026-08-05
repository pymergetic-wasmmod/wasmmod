/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_EXEC_EMBED_H_
#define PM_PM_UPY_EXEC_EMBED_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
int pm_upy_embed_init(void *heap, size_t heap_len, void *stack, size_t stack_len);
void pm_upy_embed_deinit(void);
int pm_upy_embed_exec_str(const char *src);
int pm_upy_embed_exec_mpy(const void *mpy, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_EXEC_EMBED_H_ */
