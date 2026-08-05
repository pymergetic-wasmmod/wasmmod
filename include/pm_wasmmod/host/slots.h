/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_WASMMOD_HOST_SLOTS_H_
#define PM_PM_WASMMOD_HOST_SLOTS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "pm_common.h"

void pm_wasmmod_host_clear_all(void);
bool pm_wasmmod_host_set_slot_c(int32_t slot, int32_t (*fn)(int32_t arg), void *userdata);
size_t pm_wasmmod_host_slot_count(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_HOST_SLOTS_H_ */
