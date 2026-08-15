#ifndef PM_WASMMOD_PORTS_UPY_MODGEN_H
#define PM_WASMMOD_PORTS_UPY_MODGEN_H

#include "ports/micropython/mpconfig_wasm.h"

#ifndef MICROPY_PY_WASM_GEN
#define MICROPY_PY_WASM_GEN (0)
#endif

#if MICROPY_PY_WASM_GEN

#include "py/obj.h"

#ifdef __cplusplus
extern "C" {
#endif

MP_DECLARE_CONST_FUN_OBJ_VAR_BETWEEN(mod_wasm_gen_obj);
extern const mp_obj_module_t mp_module_pymergetic_util_gen;

#ifdef __cplusplus
}
#endif

#endif /* MICROPY_PY_WASM_GEN */

#endif /* PM_WASMMOD_PORTS_UPY_MODGEN_H */
