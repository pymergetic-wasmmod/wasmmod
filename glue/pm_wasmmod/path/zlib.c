/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_wasmmod/path/zlib.h"
#include "zlibutil.h"
#include "alloc.h"
#include <stdio.h>
#include <string.h>

bool pm_wasmmod_zlib_inflate(const uint8_t *in, uint32_t in_len,
    uint8_t **out, uint32_t *out_len, char *errbuf, size_t errbuf_len) {
    const uint8_t *p = in;
    uint32_t len = in_len;
    uint8_t *owned = NULL;
    if (!mp_wasm_artifact_unwrap_zlib(&p, &len, &owned)) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "unwrap/inflate failed");
        }
        return false;
    }
    if (owned) {
        *out = owned;
        *out_len = len;
        return true;
    }
    /* Not MPZL — copy through as opaque success for non-envelope input. */
    uint8_t *buf = (uint8_t *)MICROPY_WASM_MALLOC(in_len);
    if (!buf) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "oom");
        }
        return false;
    }
    memcpy(buf, in, in_len);
    *out = buf;
    *out_len = in_len;
    return true;
}
