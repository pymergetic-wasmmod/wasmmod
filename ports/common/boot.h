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

/* Host-kernel readiness hook. Weak here and returns 0: a plain wasmmod seat has
 * no kernel under it. A downstream kernel provides the strong symbol and boots
 * itself on first call, so wasmmod needs no name from that tree.
 * 0 = ready, nonzero = boot failed. */
int pm_wasmmod_host_kernel_ready(void);

/* Init registry + builtin io_ops + loader, seed kernel fqn version, start verify trust session.
 * Keeps an io table the kernel already installed. Returns 0 on success, -1 if loader_init fails.
 * Idempotent. */
int pm_wasmmod_host_boot(const char *kernel_fqn, const char *version);

/* If name is non-empty and not yet in the registry, publish RESIDENT presence. */
void pm_wasmmod_host_presence_publish(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_COMMON_BOOT_H */
