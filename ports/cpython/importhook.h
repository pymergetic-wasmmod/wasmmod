/*
 * builtins.__import__ wrap + ensure_inited (CPython twin).
 */
#ifndef PM_WASMMOD_PORTS_CPY_IMPORTHOOK_H
#define PM_WASMMOD_PORTS_CPY_IMPORTHOOK_H

#ifndef PM_WASMMOD_CPYTHON
#define PM_WASMMOD_CPYTHON (1)
#endif
#include <Python.h>

#ifdef __cplusplus
extern "C" {
#endif

int pm_cpy_ensure_inited(void);
int pm_cpy_install_hook(void);
int pm_cpy_uninstall_hook(void);
void pm_cpy_presence_publish(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_CPY_IMPORTHOOK_H */
