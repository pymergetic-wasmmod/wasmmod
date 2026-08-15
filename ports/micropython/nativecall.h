/*
 * Typed native invoke via registry resolve + C ABI (not Value-convention call).
 * Used by wasmmod.call and pack/host export funobjs.
 */
#ifndef PM_WASMMOD_PORTS_UPY_NATIVECALL_H
#define PM_WASMMOD_PORTS_UPY_NATIVECALL_H

#include <stddef.h>

#include "py/obj.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call export using registry sig (or infer from n_args for i32-only).
 * Raises on miss / unsupported shape. Returns Python int (or None for void). */
mp_obj_t mp_wasm_native_call(const char *fqn, const char *export_name,
    size_t n_args, const mp_obj_t *args);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_UPY_NATIVECALL_H */
