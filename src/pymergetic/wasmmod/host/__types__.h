/*
 * Host marshal typedefs for cookie mem / handle obj pyexport faces.
 * Facegen `__exports__.h` includes this when sigs mention these spellings.
 */
#ifndef PYMERGETIC_WASMMOD_HOST_TYPES_H
#define PYMERGETIC_WASMMOD_HOST_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque i32 cookie into the host mem table (borrowed [ptr,len) for a call). */
typedef int32_t pm_wasmmod_mem_cookie_t;

/* Opaque i32 handle into the host obj table (µPy GC root / CPython INCREF). */
typedef int32_t pm_wasmmod_obj_handle_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_HOST_TYPES_H */
