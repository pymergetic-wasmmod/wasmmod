/*
 * pymergetic.util.gen CPython face — VfsSink ops + pyi provider + run/diff.
 * Twin of ports/micropython/modgen.c. CPython has no µPy VFS: ops are POSIX.
 */
#ifndef PM_WASMMOD_PORTS_CPY_MODGEN_H
#define PM_WASMMOD_PORTS_CPY_MODGEN_H

#ifndef PM_WASMMOD_CPYTHON
#define PM_WASMMOD_CPYTHON (1)
#endif
#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Insert pymergetic.util.gen into sys.modules. 0 ok, -1 exception. */
int pm_cpy_install_gen(void);

PyObject *pm_cpy_gen_run(PyObject *self, PyObject *args);
PyObject *pm_cpy_gen_run_vfs(PyObject *self, PyObject *args);
PyObject *pm_cpy_gen_diff(PyObject *self, PyObject *args);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_CPY_MODGEN_H */
