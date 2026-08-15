/*
 * Metal I/O ops: default DECLINE. Metal images may provide strong
 * pm_metal_wasm_io_fetch / _probe / _request (weak stubs here) for HTTP
 * park-to-completion without baking Metal headers into this portable TU.
 *
 * Always call the symbols (do not compare fn addresses to NULL): PE/COFF
 * + rust-lld weak addr checks are unreliable.
 *
 * Function pointers are filled at runtime (`pm_wasmmod_metal_io_ops_init`):
 * const-static initializers with local `static` fn addrs #UD on UEFI PE.
 */
#include "io_ops.h"

#include <stddef.h>
#include <stdint.h>

__attribute__((weak)) pm_wasmmod_io_result_t pm_metal_wasm_io_fetch(const char *uri,
    uint8_t **out_bytes, uint32_t *out_len, char *errbuf, size_t errbuf_len)
{
    (void)uri;
    (void)out_bytes;
    (void)out_len;
    (void)errbuf;
    (void)errbuf_len;
    return PM_WASMMOD_IO_DECLINE;
}

__attribute__((weak)) pm_wasmmod_io_result_t pm_metal_wasm_io_probe(const char *uri)
{
    (void)uri;
    return PM_WASMMOD_IO_DECLINE;
}

__attribute__((weak)) pm_wasmmod_io_result_t pm_metal_wasm_io_request(const char *method,
    const char *uri, const uint8_t *body, uint32_t body_len, const char *content_type,
    uint8_t **out_bytes, uint32_t *out_len, char *errbuf, size_t errbuf_len)
{
    (void)method;
    (void)uri;
    (void)body;
    (void)body_len;
    (void)content_type;
    (void)out_bytes;
    (void)out_len;
    (void)errbuf;
    (void)errbuf_len;
    return PM_WASMMOD_IO_DECLINE;
}

__attribute__((weak)) uint32_t pm_metal_async_yield(void)
{
    return 0;
}

static pm_wasmmod_io_result_t metal_io_fetch(const char *uri, uint8_t **out_bytes,
    uint32_t *out_len, char *errbuf, size_t errbuf_len)
{
    return pm_metal_wasm_io_fetch(uri, out_bytes, out_len, errbuf, errbuf_len);
}

static pm_wasmmod_io_result_t metal_io_probe(const char *uri)
{
    return pm_metal_wasm_io_probe(uri);
}

static pm_wasmmod_io_result_t metal_io_request(const char *method, const char *uri,
    const uint8_t *body, uint32_t body_len, const char *content_type, uint8_t **out_bytes,
    uint32_t *out_len, char *errbuf, size_t errbuf_len)
{
    return pm_metal_wasm_io_request(method, uri, body, body_len, content_type, out_bytes,
        out_len, errbuf, errbuf_len);
}

static void metal_io_yield(void)
{
    (void)pm_metal_async_yield();
}

/* Non-const: filled by pm_wasmmod_metal_io_ops_init before first use. */
pm_wasmmod_io_ops_t pm_wasmmod_metal_io_ops = {
    .version = PM_WASMMOD_IO_OPS_VERSION,
    .fetch = NULL,
    .probe = NULL,
    .yield = NULL,
    .request = NULL,
    .put = NULL,
    .userdata = NULL,
};

void pm_wasmmod_metal_io_ops_init(void)
{
    pm_wasmmod_metal_io_ops.fetch = metal_io_fetch;
    pm_wasmmod_metal_io_ops.probe = metal_io_probe;
    pm_wasmmod_metal_io_ops.yield = metal_io_yield;
    pm_wasmmod_metal_io_ops.request = metal_io_request;
}
