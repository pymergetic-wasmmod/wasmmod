/*
 * Metal port: replaceable pm_wasmmod_io_ops_t (async→sync park inside
 * fetch / probe / request). Stub DECLINEs all URIs until Metal fills the
 * weak hooks. Not a TLS/HTTP stack — sockets stay in Metal net.
 *
 * Call pm_wasmmod_metal_io_ops_init() before pm_wasmmod_io_set /
 * pm_wasmmod_host_boot (table starts with NULL fn-ptrs; UEFI PE #UD on
 * const-static local addrs).
 */
#ifndef PYMERGETIC_WASMMOD_PORTS_METAL_IO_OPS_H
#define PYMERGETIC_WASMMOD_PORTS_METAL_IO_OPS_H

#include "pymergetic/wasmmod/io.h" // IWYU pragma: keep

#ifdef __cplusplus
extern "C" {
#endif

extern pm_wasmmod_io_ops_t pm_wasmmod_metal_io_ops;
void pm_wasmmod_metal_io_ops_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_PORTS_METAL_IO_OPS_H */
