#include "ports/micropython/objhandle.h"

#include "py/mpstate.h"
#include "py/obj.h"

typedef struct {
    int used;
} pm_wasmmod_obj_slot_meta_t;

static pm_wasmmod_obj_slot_meta_t g_obj_meta[PM_WASMMOD_OBJ_HANDLE_SLOTS];
MP_REGISTER_ROOT_POINTER(mp_obj_t pm_wasmmod_obj_handles[PM_WASMMOD_OBJ_HANDLE_SLOTS]);

pm_wasmmod_obj_handle_t pm_wasmmod_obj_handle_put(mp_obj_t obj) {
    if (obj == MP_OBJ_NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < PM_WASMMOD_OBJ_HANDLE_SLOTS; i++) {
        if (!g_obj_meta[i].used) {
            MP_STATE_VM(pm_wasmmod_obj_handles)[i] = obj;
            g_obj_meta[i].used = 1;
            return (pm_wasmmod_obj_handle_t)(i + 1);
        }
    }
    return 0;
}

mp_obj_t pm_wasmmod_obj_handle_get(pm_wasmmod_obj_handle_t handle) {
    if (handle <= 0 || (uint32_t)handle > PM_WASMMOD_OBJ_HANDLE_SLOTS) {
        return MP_OBJ_NULL;
    }
    uint32_t i = (uint32_t)handle - 1;
    if (!g_obj_meta[i].used) {
        return MP_OBJ_NULL;
    }
    return MP_STATE_VM(pm_wasmmod_obj_handles)[i];
}

void pm_wasmmod_obj_handle_release(pm_wasmmod_obj_handle_t handle) {
    if (handle <= 0 || (uint32_t)handle > PM_WASMMOD_OBJ_HANDLE_SLOTS) {
        return;
    }
    uint32_t i = (uint32_t)handle - 1;
    MP_STATE_VM(pm_wasmmod_obj_handles)[i] = MP_OBJ_NULL;
    g_obj_meta[i].used = 0;
}
