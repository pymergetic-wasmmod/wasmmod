/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */


/* Prefer package-relative path so clangd still sees pm_upy_feat_t when -Iinclude
 * is missing (otherwise PM_UPY_FEAT_* look undeclared after a failed include). */
#include "../../include/pm_upy/features.h"
#include "../../version.h"

/* Host builds compile glue with the port include path — pick up real feature macros.
 * Require mpconfigport.h too; a bare py/mpconfig.h include fatals and poisons the TU. */
#if defined(MICROPY_PY_WASM) \
    && __has_include("py/mpconfig.h") \
    && __has_include("mpconfigport.h")
#include "py/mpconfig.h"
#endif

#ifndef MICROPY_ENABLE_GC
#define MICROPY_ENABLE_GC 0
#endif
#ifndef MICROPY_ENABLE_SCHEDULER
#define MICROPY_ENABLE_SCHEDULER 0
#endif
#ifndef MICROPY_ENABLE_COMPILER
#define MICROPY_ENABLE_COMPILER 0
#endif
#ifndef MICROPY_HELPER_REPL
#define MICROPY_HELPER_REPL 0
#endif
#ifndef MICROPY_REPL_EVENT_DRIVEN
#define MICROPY_REPL_EVENT_DRIVEN 0
#endif
#ifndef MICROPY_FLOAT_IMPL
#define MICROPY_FLOAT_IMPL 0
#endif
#ifndef MICROPY_VFS
#define MICROPY_VFS 0
#endif
#ifndef MICROPY_PY_SSL
#define MICROPY_PY_SSL 0
#endif
#ifndef MICROPY_PY_ASYNCIO
#define MICROPY_PY_ASYNCIO 0
#endif
#ifndef MICROPY_PY_SOCKET
#define MICROPY_PY_SOCKET 0
#endif
#ifndef MICROPY_PY_NETWORK
#define MICROPY_PY_NETWORK 0
#endif
#ifndef MICROPY_PY_LWIP
#define MICROPY_PY_LWIP 0
#endif
#ifndef MICROPY_PY_BLUETOOTH
#define MICROPY_PY_BLUETOOTH 0
#endif
#ifndef MICROPY_PY_WEBSOCKET
#define MICROPY_PY_WEBSOCKET 0
#endif

#undef PM_UPY_HAS_GC
#define PM_UPY_HAS_GC (MICROPY_ENABLE_GC)
#undef PM_UPY_HAS_SCHEDULER
#define PM_UPY_HAS_SCHEDULER (MICROPY_ENABLE_SCHEDULER)
#undef PM_UPY_HAS_COMPILER
#define PM_UPY_HAS_COMPILER (MICROPY_ENABLE_COMPILER)
#undef PM_UPY_HAS_REPL
#define PM_UPY_HAS_REPL (MICROPY_HELPER_REPL)
#undef PM_UPY_HAS_FLOAT
#define PM_UPY_HAS_FLOAT (MICROPY_FLOAT_IMPL != 0)
#undef PM_UPY_HAS_VFS
#define PM_UPY_HAS_VFS (MICROPY_VFS)
#undef PM_UPY_HAS_SSL
#define PM_UPY_HAS_SSL (MICROPY_PY_SSL)
#undef PM_UPY_HAS_EMBED
#define PM_UPY_HAS_EMBED 1

uint32_t pm_upy_features(void) {
    uint32_t f = PM_UPY_FEAT_NLR | PM_UPY_FEAT_READY;
#if PM_UPY_HAS_GC
    f |= PM_UPY_FEAT_GC;
#endif
#if PM_UPY_HAS_SCHEDULER
    f |= PM_UPY_FEAT_SCHEDULER;
#endif
#if PM_UPY_HAS_COMPILER
    f |= PM_UPY_FEAT_COMPILER;
#endif
#if PM_UPY_HAS_REPL
    f |= PM_UPY_FEAT_REPL;
#endif
#if PM_UPY_HAS_FLOAT
    f |= PM_UPY_FEAT_FLOAT;
#endif
#if PM_UPY_HAS_VFS
    f |= PM_UPY_FEAT_VFS;
#endif
#if PM_UPY_HAS_SSL
    f |= PM_UPY_FEAT_SSL;
#endif
#if MICROPY_PY_ASYNCIO
    f |= PM_UPY_FEAT_ASYNCIO;
#endif
#if MICROPY_PY_SOCKET
    f |= PM_UPY_FEAT_SOCKET;
#endif
#if MICROPY_PY_NETWORK
    f |= PM_UPY_FEAT_NETWORK;
#endif
#if MICROPY_PY_LWIP
    f |= PM_UPY_FEAT_LWIP;
#endif
#if MICROPY_PY_BLUETOOTH
    f |= PM_UPY_FEAT_BLUETOOTH;
#endif
#if MICROPY_PY_WEBSOCKET
    f |= PM_UPY_FEAT_WEBSOCKET;
#endif
#if PM_UPY_HAS_EMBED
    f |= PM_UPY_FEAT_EMBED;
#endif
    return f;
}

bool pm_upy_has(pm_upy_feat_t feat) {
    if (feat == PM_UPY_FEAT_NONE) {
        return true;
    }
    return (pm_upy_features() & (uint32_t)feat) == (uint32_t)feat;
}

const char *pm_upy_version(void) {
    return MICROPY_WASM_VERSION;
}

