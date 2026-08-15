/* pymergetic.wasmmod.io — host I/O table (fetch / probe / yield).
 *
 * Wait class (SOURCETREE.md): fetch / probe / request = async (Metal parks
 * inside the same symbol; mpwm may block). yield = facade. uri/join/set/get
 * = sync. Not a second module tree; not an async engine.
 */
#ifndef PYMERGETIC_WASMMOD_IO_TYPES_H
#define PYMERGETIC_WASMMOD_IO_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_WASMMOD_IO_OPS_VERSION (2u)

typedef enum {
    PM_WASMMOD_IO_OK = 0,      /* success (fetch: bytes valid; probe: exists) */
    PM_WASMMOD_IO_DECLINE = 1, /* not handled — try default backend */
    PM_WASMMOD_IO_ERR = -1,    /* handled but failed */
} pm_wasmmod_io_result_t;

typedef pm_wasmmod_io_result_t (*pm_wasmmod_io_request_t)(const char *method, const char *uri,
    const uint8_t *body, uint32_t body_len, const char *content_type, uint8_t **out_bytes,
    uint32_t *out_len, char *errbuf, size_t errbuf_len);

typedef struct pm_wasmmod_io_ops {
    uint32_t version; /* PM_WASMMOD_IO_OPS_VERSION */

    /* wait=async — GET (or equivalent). *out_bytes via MICROPY_WASM_MALLOC; caller FREE. */
    pm_wasmmod_io_result_t (*fetch)(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
        char *errbuf, size_t errbuf_len);

    /* wait=async — HTTP 200 existence. NULL → synthesize via fetch + discard. */
    pm_wasmmod_io_result_t (*probe)(const char *uri);

    /* wait=facade — optional cooperative yield during long default I/O. */
    void (*yield)(void);

    /* wait=async — write/publish. NULL / DECLINE → default http:// POST chain. */
    pm_wasmmod_io_request_t request;

    void *put; /* reserved; NULL if unused */
    void *userdata;
} pm_wasmmod_io_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_WASMMOD_IO_TYPES_H */
