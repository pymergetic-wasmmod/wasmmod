/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_WASMMOD_PACK_LOAD_H_
#define PM_PM_WASMMOD_PACK_LOAD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_common.h"
#include <stdint.h>

typedef struct pm_wasmmod_pack pm_wasmmod_pack_t;

pm_wasmmod_pack_t *pm_wasmmod_pack_load(const uint8_t *bytes, uint32_t len,
    const char *name, char *errbuf, size_t errbuf_len);
pm_wasmmod_pack_t *pm_wasmmod_pack_load_ex(const uint8_t *code, uint32_t code_len,
    const uint8_t *meta, uint32_t meta_len, const char *name, const char *path_hint,
    char *errbuf, size_t errbuf_len);
void pm_wasmmod_pack_close(pm_wasmmod_pack_t *pack);
const char *pm_wasmmod_pack_kind_str(const pm_wasmmod_pack_t *pack);
const char *pm_wasmmod_pack_origin(const pm_wasmmod_pack_t *pack);
const char *pm_wasmmod_pack_arch(const pm_wasmmod_pack_t *pack);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_PACK_LOAD_H_ */
