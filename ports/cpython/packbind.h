/*
 * Bind MPWP Python + export callables onto a loaded pack's sys.modules face.
 * CPython twin: prefer .cpy. / .py; reject .upy. / .mpy. ELF adapters included.
 */
#ifndef PM_WASMMOD_PORTS_CPY_PACKBIND_H
#define PM_WASMMOD_PORTS_CPY_PACKBIND_H

#ifndef PM_WASMMOD_CPYTHON
#define PM_WASMMOD_CPYTHON (1)
#endif

#include <Python.h>
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/wasmmod/registry/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

/* After loader_load / ELF publish: attach handle attrs, bind MPWP, exports.
 * New reference to the pack root module. */
PyObject *pm_cpy_pack_bind(const char *pack_name, pm_wasmmod_registry_handle_t h,
    const uint8_t *meta, uint32_t meta_len);

pm_wasmmod_registry_handle_t pm_cpy_elf_publish(const char *pack_name, const uint8_t *bytes,
    uint32_t len, void **img_out, char *err, size_t err_len);
void pm_cpy_store_elf_on_module(PyObject *mod, void *img);
void pm_cpy_elf_release_for_module(PyObject *mod);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_CPY_PACKBIND_H */
