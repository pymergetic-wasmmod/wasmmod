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

/* The knobs under `module`, as an object whose attributes are their leaf names:
 * reading one gives its soft value, assigning moves it, and a name below the
 * module gives the view of that card. MP_OBJ_NULL when nothing under it counts
 * anything, so a caller can leave the attribute off. */
mp_obj_t mp_wasm_limits_view(const char *module);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_UPY_HOSTREADY_H */
