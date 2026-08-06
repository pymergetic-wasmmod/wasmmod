/*
 * Wire pm_upy_resume / pm_upy_await into Metal py edge.
 * Compile this TU only in images that link both metal py and wasmmod glue.
 */
#include "register_upy.h"

#include <stdint.h>

#include "pm_upy/exec/await.h"

typedef int (*pm_metal_upy_resume_fn)(void *obj);
typedef uint32_t (*pm_metal_upy_await_fn)(uint32_t self_h, uint32_t child_h);

void pm_metal_py_set_upy_resume(pm_metal_upy_resume_fn f);
void pm_metal_py_set_upy_await(pm_metal_upy_await_fn f);

void mp_wasm_metal_register_upy(void) {
    pm_metal_py_set_upy_resume((pm_metal_upy_resume_fn)pm_upy_resume);
    pm_metal_py_set_upy_await((pm_metal_upy_await_fn)pm_upy_await);
}
