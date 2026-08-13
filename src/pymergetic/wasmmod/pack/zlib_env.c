/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <string.h>

#include "py/mpconfig.h"

#include "pymergetic/wasmmod/pack/alloc.h"
#include "pymergetic/wasmmod/pack/zlib_env.h"

#include "lib/uzlib/uzlib.h"

#ifndef MICROPY_PY_DEFLATE
#define MICROPY_PY_DEFLATE (0)
#endif

#if !MICROPY_PY_DEFLATE
// When deflate is not compiled into the port, pull the inflate objects here once.
#include "lib/uzlib/tinflate.c"
#include "lib/uzlib/header.c"
#include "lib/uzlib/adler32.c"
#include "lib/uzlib/crc32.c"
#endif

bool mp_wasm_zlib_inflate(const uint8_t *src, uint32_t src_len, uint8_t *dst, uint32_t dst_len) {
    if (src == NULL || dst == NULL || src_len == 0 || dst_len == 0) {
        return false;
    }
    uzlib_uncomp_t decomp;
    memset(&decomp, 0, sizeof(decomp));
    decomp.source = src;
    decomp.source_limit = src + src_len;
    decomp.dest_start = dst;
    decomp.dest = dst;
    decomp.dest_limit = dst + dst_len;

    int wbits = 0;
    if (uzlib_parse_zlib_gzip_header(&decomp, &wbits) != UZLIB_HEADER_ZLIB) {
        return false;
    }
    if (wbits < 8) {
        wbits = 8;
    }
    if (wbits > 15) {
        wbits = 15;
    }
    size_t dict_len = (size_t)1 << (unsigned)wbits;
    uint8_t *dict = MICROPY_WASM_MALLOC(dict_len);
    if (dict == NULL) {
        return false;
    }
    uzlib_uncompress_init(&decomp, dict, (unsigned int)dict_len);
    int st;
    do {
        st = uzlib_uncompress_chksum(&decomp);
    } while (st == UZLIB_OK);
    MICROPY_WASM_FREE(dict);
    return st == UZLIB_DONE && (uint32_t)(decomp.dest - decomp.dest_start) == dst_len;
}

bool mp_wasm_artifact_unwrap_zlib(const uint8_t **inout, uint32_t *inout_len, uint8_t **owned) {
    if (owned != NULL) {
        *owned = NULL;
    }
    if (inout == NULL || *inout == NULL || inout_len == NULL || *inout_len < 8) {
        return true;
    }
    if (memcmp(*inout, MP_WASM_ARTIFACT_ZLIB_MAGIC, 4) != 0) {
        return true;
    }
    uint32_t raw_len = (uint32_t)(*inout)[4]
        | ((uint32_t)(*inout)[5] << 8)
        | ((uint32_t)(*inout)[6] << 16)
        | ((uint32_t)(*inout)[7] << 24);
    if (raw_len == 0 || raw_len > (64u * 1024u * 1024u)) {
        return false;
    }
    const uint8_t *zsrc = *inout + 8;
    uint32_t zlen = *inout_len - 8;
    uint8_t *dst = MICROPY_WASM_MALLOC(raw_len);
    if (dst == NULL) {
        return false;
    }
    if (!mp_wasm_zlib_inflate(zsrc, zlen, dst, raw_len)) {
        MICROPY_WASM_FREE(dst);
        return false;
    }
    *inout = dst;
    *inout_len = raw_len;
    if (owned != NULL) {
        *owned = dst;
    }
    return true;
}
