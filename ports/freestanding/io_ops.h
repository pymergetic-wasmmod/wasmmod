/*
 * Freestanding port: a replaceable pm_wasmmod_io_ops_t whose fetch / probe /
 * request park async→sync. Every URI DECLINEs until the host kernel under this
 * image defines the strong pm_wasmmod_host_io_* hooks below. Not a TLS/HTTP
 * stack — sockets belong to that kernel, and wasmmod names none of it.
 *
 * Call pm_wasmmod_host_io_ops_init() before pm_wasmmod_io_set /
 * pm_wasmmod_host_boot (table starts with NULL fn-ptrs; UEFI PE #UD on
 * const-static local addrs).
 */
#ifndef PYMERGETIC_WASMMOD_PORTS_FREESTANDING_IO_OPS_H
#define PYMERGETIC_WASMMOD_PORTS_FREESTANDING_IO_OPS_H

#include "pymergetic/wasmmod/io.h" // IWYU pragma: keep

#ifdef __cplusplus
extern "C" {
#endif

extern pm_wasmmod_io_ops_t pm_wasmmod_host_io_ops;
void pm_wasmmod_host_io_ops_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_PORTS_FREESTANDING_IO_OPS_H */
