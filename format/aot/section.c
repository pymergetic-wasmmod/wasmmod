/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "extmod/wasmmod/format/aot/section.h"

#include <string.h>

#include "extmod/wasmmod/alloc.h"

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool mp_wasm_aot_find_section(const uint8_t *buf, uint32_t len, const char *name,
    const uint8_t **payload, uint32_t *payload_len) {
    if (buf == NULL || len < 8 || name == NULL) {
        return false;
    }
    if (!(buf[0] == 0x00 && buf[1] == 'a' && buf[2] == 'o' && buf[3] == 't')) {
        return false;
    }
    const uint8_t *end = buf + len;
    const size_t want_len = strlen(name);
    uintptr_t p = 8;
    while (p + 8 <= len) {
        uint32_t typ = read_u32_le(buf + p);
        uint32_t size = read_u32_le(buf + p + 4);
        const uint8_t *content = buf + p + 8;
        if (content + size > end || size > 0x10000000u) {
            return false;
        }
        if (typ == 100 && size >= 6) {
            uint32_t sub = read_u32_le(content);
            if (sub == 0) {
                uint16_t slen = read_u16_le(content + 4);
                const uint8_t *nb = content + 6;
                if (nb + slen <= content + size) {
                    size_t bare = slen;
                    if (bare > 0 && nb[bare - 1] == 0) {
                        bare--;
                    }
                    if (bare == want_len && memcmp(nb, name, want_len) == 0) {
                        *payload = nb + slen;
                        *payload_len = (uint32_t)((content + size) - (nb + slen));
                        return true;
                    }
                }
            }
        }
        p = ((uintptr_t)(content + size - buf) + 3u) & ~(uintptr_t)3u;
    }
    return false;
}

bool mp_wasm_aot_strip_section(const uint8_t *buf, uint32_t len, const char *name,
    uint8_t **out, uint32_t *out_len) {
    if (buf == NULL || len < 8 || name == NULL
        || !(buf[0] == 0x00 && buf[1] == 'a' && buf[2] == 'o' && buf[3] == 't')) {
        return false;
    }
    uint8_t *dst = MICROPY_WASM_MALLOC(len);
    if (dst == NULL) {
        return false;
    }
    memcpy(dst, buf, 8);
    uint32_t w = 8;
    const size_t want_len = strlen(name);
    uintptr_t p = 8;
    while (p + 8 <= len) {
        const uint8_t *sec_start = buf + p;
        uint32_t typ = read_u32_le(buf + p);
        uint32_t size = read_u32_le(buf + p + 4);
        const uint8_t *content = buf + p + 8;
        if (content + size > buf + len || size > 0x10000000u) {
            MICROPY_WASM_FREE(dst);
            return false;
        }
        uintptr_t aligned = ((uintptr_t)(content + size - buf) + 3u) & ~(uintptr_t)3u;
        uintptr_t next = aligned <= len ? aligned : len;
        bool skip = false;
        if (typ == 100 && size >= 6) {
            uint32_t sub = read_u32_le(content);
            if (sub == 0) {
                uint16_t slen = read_u16_le(content + 4);
                const uint8_t *nb = content + 6;
                if (nb + slen <= content + size) {
                    size_t bare = slen;
                    if (bare > 0 && nb[bare - 1] == 0) {
                        bare--;
                    }
                    if (bare == want_len && memcmp(nb, name, want_len) == 0) {
                        skip = true;
                    }
                }
            }
        }
        if (!skip) {
            size_t n = (size_t)(next - p);
            memcpy(dst + w, sec_start, n);
            w += (uint32_t)n;
        } else {
            break;
        }
        p = next;
    }
    *out = dst;
    *out_len = w;
    return true;
}
