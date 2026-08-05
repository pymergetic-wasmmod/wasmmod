/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */
#ifndef PM_PM_WASMMOD_PATH_VERIFY_H_
#define PM_PM_WASMMOD_PATH_VERIFY_H_

#include "pm_common.h"

#ifdef __cplusplus
extern "C" {
#endif

bool pm_wasmmod_trust_add(const uint8_t *key, size_t key_len);
void pm_wasmmod_trust_clear(void);
size_t pm_wasmmod_trust_count(void);
bool pm_wasmmod_verify_bytes(const uint8_t *bytes, uint32_t len,
    const char *path_hint, char *errbuf, size_t errbuf_len);
void pm_wasmmod_set_verify_enabled(bool on);
bool pm_wasmmod_get_verify_enabled(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_PATH_VERIFY_H_ */
