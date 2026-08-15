/*
 * Typed native invoke via registry resolve + C ABI (CPython twin).
 */
#ifndef PM_WASMMOD_PORTS_CPY_NATIVECALL_H
#define PM_WASMMOD_PORTS_CPY_NATIVECALL_H

#ifndef PM_WASMMOD_CPYTHON
#define PM_WASMMOD_CPYTHON (1)
#endif
#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call export using registry sig. Raises on miss / unsupported shape.
 * Returns a new reference (typically int). */
PyObject *pm_cpy_native_call(const char *fqn, const char *export_name, PyObject *args_tuple);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_CPY_NATIVECALL_H */
