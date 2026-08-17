/* pymergetic.wasmmod.net.cdn µPy face (modcdn.c). Same module on unix,
 * firmware, and emcc. Unix already has mp_module_pymergetic_wasmmod. */
#ifndef PYMERGETIC_WASMMOD_PORTS_MICROPYTHON_MODCDN_H
#define PYMERGETIC_WASMMOD_PORTS_MICROPYTHON_MODCDN_H

#include "py/obj.h"

#include "ports/micropython/mpconfig_wasm.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const mp_obj_module_t mp_module_pymergetic_wasmmod_net;
extern const mp_obj_module_t mp_module_pymergetic_wasmmod_net_cdn;
#if MICROPY_WASM_FREESTANDING
extern const mp_obj_module_t mp_module_pymergetic_wasmmod;
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_PORTS_MICROPYTHON_MODCDN_H */
