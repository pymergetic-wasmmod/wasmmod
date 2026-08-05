/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef PM_PM_UPY_FEATURES_H_
#define PM_PM_UPY_FEATURES_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PM_UPY_FEAT_NONE = 0,
    PM_UPY_FEAT_GC = 1u << 0,
    PM_UPY_FEAT_SCHEDULER = 1u << 1,
    PM_UPY_FEAT_COMPILER = 1u << 2,
    PM_UPY_FEAT_REPL = 1u << 3,
    PM_UPY_FEAT_FLOAT = 1u << 4,
    PM_UPY_FEAT_VFS = 1u << 5,
    PM_UPY_FEAT_SSL = 1u << 6,
    PM_UPY_FEAT_ASYNCIO = 1u << 7,
    PM_UPY_FEAT_SOCKET = 1u << 8,
    PM_UPY_FEAT_HW = 1u << 9,
    PM_UPY_FEAT_EMBED = 1u << 10,
    PM_UPY_FEAT_NLR = 1u << 11,
    PM_UPY_FEAT_READY = 1u << 12,
    PM_UPY_FEAT_NETWORK = 1u << 13,
    PM_UPY_FEAT_LWIP = 1u << 14,
    PM_UPY_FEAT_BLUETOOTH = 1u << 15,
    PM_UPY_FEAT_WEBSOCKET = 1u << 16,
} pm_upy_feat_t;


#ifndef PM_UPY_HAS_GC
#define PM_UPY_HAS_GC 0
#endif
#ifndef PM_UPY_HAS_SCHEDULER
#define PM_UPY_HAS_SCHEDULER 0
#endif
#ifndef PM_UPY_HAS_COMPILER
#define PM_UPY_HAS_COMPILER 0
#endif
#ifndef PM_UPY_HAS_REPL
#define PM_UPY_HAS_REPL 0
#endif
#ifndef PM_UPY_HAS_FLOAT
#define PM_UPY_HAS_FLOAT 0
#endif
#ifndef PM_UPY_HAS_VFS
#define PM_UPY_HAS_VFS 0
#endif
#ifndef PM_UPY_HAS_SSL
#define PM_UPY_HAS_SSL 0
#endif
#ifndef PM_UPY_HAS_EMBED
#define PM_UPY_HAS_EMBED 0
#endif

uint32_t pm_upy_features(void);
bool pm_upy_has(pm_upy_feat_t feat);
const char *pm_upy_version(void);

/* Guest imports (micropython.runtime) — same header as host API.
 * pm_guest.h defines PM_WASMMOD_GUEST (do not -D it away on the host). */
#include "pm_guest.h"
#if PM_WASMMOD_GUEST
MP_WASM_IMPORT("micropython.runtime", uint32_t, features, void);
MP_WASM_IMPORT("micropython.runtime", int32_t, has, int32_t feat);
#endif

#ifdef __cplusplus
}
#endif


#endif /* PM_PM_UPY_FEATURES_H_ */
