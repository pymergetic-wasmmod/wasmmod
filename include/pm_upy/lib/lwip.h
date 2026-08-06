/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * lwIP stack hooks used by µPy ports (`MICROPY_PY_LWIP`).
 */

#ifndef PM_PM_UPY_LIB_LWIP_H_
#define PM_PM_UPY_LIB_LWIP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** 1 if `MICROPY_PY_LWIP` is built in. */
int pm_upy_lwip_available(void);

/** `mod_network_lwip_init()` — `PM_ERR_FEATURE` if lwIP off. */
int pm_upy_lwip_init(void);

/** `mod_network_lwip_poll_wrapper(ticks_ms)` — no-op / feature-fail if off. */
int pm_upy_lwip_poll(uint32_t ticks_ms);

#include <stdint.h>
int pm_upy_lwip_gethostbyname(const char *host, uint8_t ip[4]);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LIB_LWIP_H_ */
