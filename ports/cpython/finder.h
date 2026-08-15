/*
 * Pack path finder for the CPython host (ports/cpython).
 *
 * Same candidate forms as µPy: dotted / slash / __init__ + .elf/.aotN/.aot/.wasm
 * (+ .zlib). HTTP roots use pymergetic.wasmmod.io. Local roots use POSIX
 * stat/fopen (CPython has no µPy VFS). Metal-cdn bases skip flat HTTP probes.
 */
#ifndef PM_WASMMOD_PORTS_CPY_FINDER_H
#define PM_WASMMOD_PORTS_CPY_FINDER_H

#ifndef PM_WASMMOD_CPYTHON
#define PM_WASMMOD_CPYTHON (1)
#endif

#include <Python.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/wasmmod/registry/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

bool pm_cpy_is_host_face(const char *dotted_name);

/* Search wasm.path then sys.path. On success writes a NUL-terminated path. */
bool pm_cpy_find_pack(const char *dotted_name, char *path_out, size_t path_cap);

PyObject *pm_cpy_path_obj(void);
void pm_cpy_path_append(const char *root);

/* Local path: POSIX. http(s) URI: io.fetch (MICROPY_WASM_MALLOC). */
bool pm_cpy_read_file(const char *path, uint8_t **out, size_t *out_len);

/* Find + read + load + sys.modules face. New reference; NULL + exception. */
PyObject *pm_cpy_import_pack(const char *dotted_name);

void pm_cpy_store_handle_on_module(PyObject *mod, pm_wasmmod_registry_handle_t h);
bool pm_cpy_load_handle_from_module(PyObject *mod, pm_wasmmod_registry_handle_t *out);

void pm_cpy_unload_pack(const char *dotted_name);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_CPY_FINDER_H */
