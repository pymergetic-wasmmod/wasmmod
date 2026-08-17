/* pymergetic.wasmmod.net.cdn µPy face (modcdn.c). Same module on unix,
 * firmware, and emcc. Unix already has mp_module_pymergetic_wasmmod. */
#ifndef PYMERGETIC_WASMMOD_PORTS_MICROPYTHON_MODCDN_H
#define PYMERGETIC_WASMMOD_PORTS_MICROPYTHON_MODCDN_H

#include "py/obj.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const mp_obj_module_t mp_module_pymergetic_wasmmod_net;
extern const mp_obj_module_t mp_module_pymergetic_wasmmod_net_cdn;
/* Defined here or in modwasmmod.c, depending on MICROPY_PY_WASM_FULL. */
extern const mp_obj_module_t mp_module_pymergetic_wasmmod;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_PORTS_MICROPYTHON_MODCDN_H */
