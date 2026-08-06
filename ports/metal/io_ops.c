/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * Metal I/O ops: default DECLINE. Metal images may provide strong
 * pm_metal_wasm_io_fetch / _probe (weak stubs here) for HTTP park-to-completion
 * without baking Metal headers into this portable TU.
 *
 * Always call the symbols (do not compare fn addresses to NULL): PE/COFF
 * + rust-lld weak addr checks are unreliable.
 *
 * Function pointers are filled at runtime (`mp_wasm_metal_io_ops_init`):
 * const-static initializers with local `static` fn addrs #UD on UEFI PE.
 */
#include "io_ops.h"

#include <stddef.h>

__attribute__((weak)) mp_wasm_io_result_t pm_metal_wasm_io_fetch(const char *uri,
	uint8_t **out_bytes, uint32_t *out_len, char *errbuf, size_t errbuf_len)
{
	(void)uri;
	(void)out_bytes;
	(void)out_len;
	(void)errbuf;
	(void)errbuf_len;
	return MP_WASM_IO_DECLINE;
}

__attribute__((weak)) mp_wasm_io_result_t pm_metal_wasm_io_probe(const char *uri)
{
	(void)uri;
	return MP_WASM_IO_DECLINE;
}

__attribute__((weak)) uint32_t pm_metal_async_yield(void)
{
	return 0;
}

static mp_wasm_io_result_t metal_io_fetch(const char *uri, uint8_t **out_bytes,
	uint32_t *out_len, char *errbuf, size_t errbuf_len)
{
	return pm_metal_wasm_io_fetch(uri, out_bytes, out_len, errbuf, errbuf_len);
}

static mp_wasm_io_result_t metal_io_probe(const char *uri)
{
	return pm_metal_wasm_io_probe(uri);
}

static void metal_io_yield(void)
{
	(void)pm_metal_async_yield();
}

/* Non-const: filled by mp_wasm_metal_io_ops_init before first use. */
mp_wasm_io_ops_t mp_wasm_metal_io_ops = {
	.version = MP_WASM_IO_OPS_VERSION,
	.fetch = NULL,
	.probe = NULL,
	.yield = NULL,
	.request = NULL,
	.put = NULL,
	.userdata = NULL,
};

void mp_wasm_metal_io_ops_init(void)
{
	mp_wasm_metal_io_ops.fetch = metal_io_fetch;
	mp_wasm_metal_io_ops.probe = metal_io_probe;
	mp_wasm_metal_io_ops.yield = metal_io_yield;
}
