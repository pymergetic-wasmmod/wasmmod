/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * MicroPython `network` module face (NIC / hostname). Full NIC socket protocol
 * stays inside µPy; this is the host control / probe surface.
 */

#ifndef PM_PM_UPY_LIB_NETWORK_H_
#define PM_PM_UPY_LIB_NETWORK_H_

#ifdef __cplusplus
extern "C" {
#endif

/** 1 if `MICROPY_PY_NETWORK` is built in. */
int pm_upy_network_available(void);

/**
 * Current `network.hostname` buffer (empty string if network off).
 * Pointer is to µPy static storage; do not free.
 */
const char *pm_upy_network_hostname(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_UPY_LIB_NETWORK_H_ */
