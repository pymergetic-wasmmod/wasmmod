/*
 * µPy import hook + host ensure_inited (wraps ports/common/boot).
 */
#ifndef PM_WASMMOD_PORTS_UPY_IMPORTHOOK_H
#define PM_WASMMOD_PORTS_UPY_IMPORTHOOK_H

#include "py/obj.h"

#ifdef __cplusplus
extern "C" {
#endif

void mp_wasm_ensure_inited(void);
void mp_wasm_presence_publish(const char *name);

MP_DECLARE_CONST_FUN_OBJ_0(mod_wasm_install_hook_obj);
MP_DECLARE_CONST_FUN_OBJ_0(mod_wasm_uninstall_hook_obj);
MP_DECLARE_CONST_FUN_OBJ_1(mod_wasm_publish_presence_obj);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_UPY_IMPORTHOOK_H */
