/*
 * Builtin pymergetic.util — nest for util.gen + host FS leaves under HOST_SRC.
 * Fixed ROM globals hold gen; ATTR_DELEGATION supplies __path__ and mutable
 * child stores (so import can attach pysample/lz4 onto the package).
 */
#ifndef PM_WASMMOD_PORTS_UPY_MODUTIL_H
#define PM_WASMMOD_PORTS_UPY_MODUTIL_H

#include "py/obj.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const mp_obj_module_t mp_module_pymergetic_util;

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_UPY_MODUTIL_H */
