/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Open wasmmod.source for the running host engine (pymergetic.wasmmod).
 */

#ifndef PM_PM_WASMMOD_HOST_SELF_H_
#define PM_PM_WASMMOD_HOST_SELF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pm_common.h"
#include "pm_wasmmod/inspect/source.h"

/** Canonical first-party engine package name (`pymergetic.wasmmod`). */
const char *pm_wasmmod_host_package_name(void);

/**
 * Resolve a filesystem path to the running host image when known
 * (e.g. Linux `/proc/self/exe`). Writes a NUL-terminated path on PM_OK.
 */
pm_status_t pm_wasmmod_host_self_path(char *buf, size_t buflen);

/**
 * Pin host image bytes when a path is unavailable (browser).
 * If take_ownership is true, bytes are freed with the host wasm allocator
 * on replace or clear. Pass buf=NULL / len=0 to clear.
 */
void pm_wasmmod_host_set_self_image(const uint8_t *buf, uint32_t len, bool take_ownership);

/**
 * Borrowed pointer to the pinned self-image buffer (browser / set_self_image).
 * NULL / *len_out=0 when unset. Does not load from filesystem.
 */
const uint8_t *pm_wasmmod_host_self_image_ptr(uint32_t *len_out);

/**
 * Open wasmmod.source for the running host. Caller closes with
 * pm_wasmmod_source_close. NULL if the image cannot be resolved or has
 * no source section.
 */
pm_wasmmod_source_t *pm_wasmmod_host_self_open(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_HOST_SELF_H_ */
