/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */
#ifndef PM_PM_WASMMOD_PACK_MEM_H_
#define PM_PM_WASMMOD_PACK_MEM_H_

#include "pm_wasmmod/pack/load.h"

#ifdef __cplusplus
extern "C" {
#endif

bool pm_wasmmod_pack_linear(pm_wasmmod_pack_t *pack, uint32_t off, uint32_t n, void **out);
bool pm_wasmmod_pack_mem_read(pm_wasmmod_pack_t *pack, uint32_t off, uint32_t n, void *dst);
bool pm_wasmmod_pack_mem_write(pm_wasmmod_pack_t *pack, uint32_t off, uint32_t n, const void *src);
uint32_t pm_wasmmod_pack_mem_alloc(pm_wasmmod_pack_t *pack, uint32_t n, void **native_out);
void pm_wasmmod_pack_mem_free(pm_wasmmod_pack_t *pack, uint32_t off);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_PACK_MEM_H_ */
