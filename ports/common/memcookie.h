/*
 * Host mem-cookie table — C ABI (µPy + future CPython).
 * Cookies are 1-based slot indices; 0 is invalid.
 * put() borrows [ptr,len) for the lifetime of the cookie (caller keeps buffer live).
 */
#ifndef PM_WASMMOD_PORTS_COMMON_MEMCOOKIE_H
#define PM_WASMMOD_PORTS_COMMON_MEMCOOKIE_H

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/wasmmod/host/__types__.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_WASMMOD_MEM_COOKIE_SLOTS 32

/* Register a borrow. Returns cookie > 0, or 0 on full/bad args. */
pm_wasmmod_mem_cookie_t pm_wasmmod_mem_cookie_put(const uint8_t *ptr, uint32_t len);

/* Lookup. Returns 0 on ok, -1 on bad cookie. */
int pm_wasmmod_mem_cookie_get(pm_wasmmod_mem_cookie_t cookie, const uint8_t **ptr_out,
    uint32_t *len_out);

void pm_wasmmod_mem_cookie_release(pm_wasmmod_mem_cookie_t cookie);

#ifdef __cplusplus
}
#endif

#endif /* PM_WASMMOD_PORTS_COMMON_MEMCOOKIE_H */
