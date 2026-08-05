/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_wasmmod/path/fetch.h"
#include "fetch.h"
#include "alloc.h"
#include "py/misc.h"
#include <stdio.h>
#include <string.h>

bool pm_wasmmod_fetch(const char *url, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    vstr_t vstr;
    vstr_init(&vstr, 128);
    if (!mp_wasm_fetch(url, &vstr, errbuf, errbuf_len)) {
        vstr_clear(&vstr);
        return false;
    }
    uint8_t *buf = (uint8_t *)MICROPY_WASM_MALLOC(vstr.len);
    if (!buf) {
        vstr_clear(&vstr);
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "oom");
        }
        return false;
    }
    memcpy(buf, vstr.buf, vstr.len);
    *out_bytes = buf;
    *out_len = (uint32_t)vstr.len;
    vstr_clear(&vstr);
    return true;
}
