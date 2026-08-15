/*
 * After import: presence is published; install live py thunks and/or attach
 * registry exports as Python callables so C/RS/py share one ready path.
 */
#ifndef PM_WASMMOD_PORTS_UPY_HOSTREADY_H
#define PM_WASMMOD_PORTS_UPY_HOSTREADY_H

#include "py/obj.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Idempotent. Safe for packs + host leaves. Returns bind_py count, or -1. */
int mp_wasm_host_ready(const char *fqn, mp_obj_t module);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_UPY_HOSTREADY_H */
