/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_WASMMOD_PATH_CDN_H_
#define PM_PM_WASMMOD_PATH_CDN_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_common.h"

void pm_wasmmod_cdn_configure(const char *base_url, const char *token);
void pm_wasmmod_cdn_reset(void);
bool pm_wasmmod_cdn_add(const char *base_url, const char *token);
bool pm_wasmmod_cdn_prepend(const char *base_url, const char *token);
bool pm_wasmmod_cdn_fetch_pack(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len, char *errbuf, size_t errbuf_len);
bool pm_wasmmod_cdn_fetch_index(const char *channel,
    uint8_t **out_bytes, uint32_t *out_len, char *errbuf, size_t errbuf_len);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_PATH_CDN_H_ */
