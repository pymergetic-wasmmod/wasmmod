/*
 * Register metalpython await/resume into Metal py edge (optional link).
 *
 * Call after pm_upy_init / embed when the integration image links wasmmod:
 *
 *   #include "extmod/wasmmod/ports/metal/register_upy.h"
 *   mp_wasm_metal_register_upy();
 *
 * No-ops if pm_metal_py_set_upy_* are missing (weak). Keeps experimental3
 * images that do not link metalpython building cleanly when this file is
 * not compiled in.
 */
#ifndef MICROPY_INCLUDED_WASMMOD_PORTS_METAL_REGISTER_UPY_H
#define MICROPY_INCLUDED_WASMMOD_PORTS_METAL_REGISTER_UPY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void mp_wasm_metal_register_upy(void);

#ifdef __cplusplus
}
#endif

#endif
