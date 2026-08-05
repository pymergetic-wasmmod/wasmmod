/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */
#ifndef PM_PM_WASMMOD_PACK_CALL_H_
#define PM_PM_WASMMOD_PACK_CALL_H_

#include "pm_wasmmod/pack/load.h"

#ifdef __cplusplus
extern "C" {
#endif

bool pm_wasmmod_pack_call_0(pm_wasmmod_pack_t *pack, const char *func,
    int32_t *out_result, char *errbuf, size_t errbuf_len);
bool pm_wasmmod_pack_call_i32(pm_wasmmod_pack_t *pack, const char *func,
    const int32_t *args, uint32_t nargs, int32_t *out_result,
    char *errbuf, size_t errbuf_len);
void *pm_wasmmod_pack_lookup_fn(pm_wasmmod_pack_t *pack, const char *func);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_PACK_CALL_H_ */
