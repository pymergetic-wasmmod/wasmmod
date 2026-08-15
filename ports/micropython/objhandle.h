/*
 * Host obj-handle table — µPy GC-rooted mp_obj_t slots.
 * Handles are 1-based; 0 is invalid.
 */
#ifndef PM_WASMMOD_PORTS_UPY_OBJHANDLE_H
#define PM_WASMMOD_PORTS_UPY_OBJHANDLE_H

#include "py/obj.h"
#include "pymergetic/wasmmod/host/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_WASMMOD_OBJ_HANDLE_SLOTS 32

pm_wasmmod_obj_handle_t pm_wasmmod_obj_handle_put(mp_obj_t obj);
mp_obj_t pm_wasmmod_obj_handle_get(pm_wasmmod_obj_handle_t handle);
void pm_wasmmod_obj_handle_release(pm_wasmmod_obj_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_UPY_OBJHANDLE_H */
