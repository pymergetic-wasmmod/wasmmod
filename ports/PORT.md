/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * Port replaceability for Metal (and other hosts).
 *
 * wasmmod keeps MicroPython-facing APIs sync. Hosts that are async-heavy
 * (Metal) replace the I/O table so each sync call runs / parks an async
 * routine to completion. Call sites (finder, load_pack, verify `.sig`) never
 * talk to sockets directly.
 *
 * ## I/O ops (preferred)
 *
 * ```c
 * #include "extmod/wasmmod/io.h"
 *
 * static mp_wasm_io_result_t metal_fetch(const char *uri, uint8_t **out,
 *     uint32_t *out_len, char *err, size_t err_len) {
 *     if (!mp_wasm_uri_is_http(uri)) {
 *         return MP_WASM_IO_DECLINE; // VFS still handled by wasmmod default
 *     }
 *     // await metal_http_get(uri) → fill *out / *out_len with MICROPY_WASM_MALLOC
 *     return MP_WASM_IO_OK; // or MP_WASM_IO_ERR
 * }
 *
 * static mp_wasm_io_result_t metal_probe(const char *uri) {
 *     if (!mp_wasm_uri_is_http(uri)) {
 *         return MP_WASM_IO_DECLINE;
 *     }
 *     // await metal_http_head(uri) → OK / ERR
 *     return MP_WASM_IO_OK;
 * }
 *
 * static const mp_wasm_io_ops_t metal_io = {
 *     .version = MP_WASM_IO_OPS_VERSION,
 *     .fetch = metal_fetch,
 *     .probe = metal_probe,
 *     .yield = metal_sched_yield, // optional
 * };
 *
 * // Compile-time:
 * //   #define MICROPY_WASM_IO_OPS metal_io
 * // Runtime (after init):
 * //   mp_wasm_io_set(&metal_io);
 * ```
 *
 * Return codes:
 * - `MP_WASM_IO_OK` — done (fetch filled buffer; probe = exists)
 * - `MP_WASM_IO_DECLINE` — not your URI; wasmmod tries the next backend
 * - `MP_WASM_IO_ERR` — you handled it and it failed (finder tries next candidate)
 *
 * ### Async later
 *
 * `mp_wasm_io_ops_t.reserved0/1` are reserved for `fetch_async` / `probe_async`
 * function pointers. Until then, Metal should block or park inside `fetch` /
 * `probe`. When async slots land, wasmmod can grow optional non-blocking
 * entry points without changing pack format or Python APIs.
 *
 * ## Other hooks
 *
 * | Hook | Role |
 * |------|------|
 * | `MICROPY_WASM_MALLOC` / `REALLOC` / `FREE` | instance + fetch buffers |
 * | `MICROPY_WASM_IO_OPS` | default `mp_wasm_io_ops_t` symbol |
 * | `MICROPY_WASM_FETCH(uri, &buf, &len, err, errlen)` | legacy bool shim (prefer IO_OPS) |
 * | `MICROPY_WASM_VERIFY_HOOK(bytes, len, sig, siglen)` | replace mbedtls verify |
 * | `MICROPY_WASM_EXPORT_PUBLISH(mod, name, fn)` | observe guest exports (e.g. Metal `reg`) |
 * | `MICROPY_WASM_STACK_SIZE` / `HEAP_SIZE` | WAMR instance sizing |
 *
 * Defaults: POSIX HTTP(S) in `fetch.c` when `MICROPY_WASM_HTTP_NATIVE=1`;
 * VFS via `mp_reader`; verify via mbedtls when `MICROPY_WASM_VERIFY!=0`.
 */
