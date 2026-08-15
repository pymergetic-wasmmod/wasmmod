/*
 * After import: bind_py thunks + attach registry exports as callables.
 */
#ifndef PM_WASMMOD_PORTS_CPY_HOSTREADY_H
#define PM_WASMMOD_PORTS_CPY_HOSTREADY_H

#ifndef PM_WASMMOD_CPYTHON
#define PM_WASMMOD_CPYTHON (1)
#endif
#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns bind count, or -1. */
int pm_cpy_host_ready(const char *fqn, PyObject *module);

/* Callable that routes to pm_cpy_native_call(fqn, export_name, args). */
PyObject *pm_cpy_make_export_fun(const char *fqn, const char *export_name);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_CPY_HOSTREADY_H */
