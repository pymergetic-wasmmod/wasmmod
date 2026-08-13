/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pymergetic/wasmmod/pack/format/wasm/section.h"

#include <string.h>

#include "pymergetic/wasmmod/pack/alloc.h"
#include "pymergetic/wasmmod/pack/__types__.h"

bool mp_wasm_wasm_find_section(const uint8_t *buf, uint32_t len, const char *name,
    const uint8_t **payload, uint32_t *payload_len) {
    if (buf == NULL || len < 8 || name == NULL) {
        return false;
    }
    if (!(buf[0] == 0x00 && buf[1] == 'a' && buf[2] == 's' && buf[3] == 'm')) {
        return false;
    }
    const uint8_t *p = buf + 8;
    const uint8_t *end = buf + len;
    const size_t want_len = strlen(name);

    while (p < end) {
        uint8_t id = *p++;
        uint32_t size;
        if (!mp_wasm_read_uleb(&p, end, &size) || p + size > end) {
            return false;
        }
        const uint8_t *sec = p;
        p += size;
        if (id != 0) {
            continue;
        }
        const uint8_t *q = sec;
        uint32_t name_len;
        if (!mp_wasm_read_uleb(&q, sec + size, &name_len) || q + name_len > sec + size) {
            continue;
        }
        if (name_len == want_len && memcmp(q, name, want_len) == 0) {
            q += name_len;
            *payload = q;
            *payload_len = (uint32_t)((sec + size) - q);
            return true;
        }
    }
    return false;
}

bool mp_wasm_wasm_strip_section(const uint8_t *buf, uint32_t len, const char *name,
    uint8_t **out, uint32_t *out_len) {
    if (buf == NULL || len < 8 || name == NULL
        || !(buf[0] == 0x00 && buf[1] == 'a' && buf[2] == 's' && buf[3] == 'm')) {
        return false;
    }
    uint8_t *dst = MICROPY_WASM_MALLOC(len);
    if (dst == NULL) {
        return false;
    }
    memcpy(dst, buf, 8);
    uint32_t w = 8;
    const size_t want_len = strlen(name);
    const uint8_t *p = buf + 8;
    const uint8_t *end = buf + len;
    while (p < end) {
        const uint8_t *sec_start = p;
        uint8_t id = *p++;
        uint32_t size;
        if (!mp_wasm_read_uleb(&p, end, &size) || p + size > end) {
            MICROPY_WASM_FREE(dst);
            return false;
        }
        const uint8_t *payload = p;
        p += size;
        bool skip = false;
        if (id == 0) {
            const uint8_t *q = payload;
            uint32_t name_len;
            if (mp_wasm_read_uleb(&q, payload + size, &name_len)
                && q + name_len <= payload + size
                && name_len == want_len
                && memcmp(q, name, want_len) == 0) {
                skip = true;
            }
        }
        if (!skip) {
            size_t n = (size_t)(p - sec_start);
            memcpy(dst + w, sec_start, n);
            w += (uint32_t)n;
        }
    }
    *out = dst;
    *out_len = w;
    return true;
}
