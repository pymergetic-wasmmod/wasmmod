/*
 * Host boot glue shared by µPy and (future) CPython ports.
 * No Python runtime types — only registry / loader / verify session.
 */
#ifndef PM_WASMMOD_PORTS_COMMON_BOOT_H
#define PM_WASMMOD_PORTS_COMMON_BOOT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Weak no-op; Metal provides strong (MICROPY_PY_METAL=1). Metal module __init__ calls this
 * after Python PM_MOD_BOOT* have queued extras. */
int pm_metal_boot(void);

/* Init registry + builtin io_ops + loader, seed kernel fqn version, start verify trust session.
 * Keeps an io table already installed (Metal). Returns 0 on success, -1 if loader_init fails.
 * Idempotent. */
int pm_wasmmod_host_boot(const char *kernel_fqn, const char *version);

/* If name is non-empty and not yet in the registry, publish RESIDENT presence. */
void pm_wasmmod_host_presence_publish(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_COMMON_BOOT_H */
