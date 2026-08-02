/*
 * This file is part of the MicroPython project, http://micropython.org/
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

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "py/reader.h"
#include "py/runtime.h"

#include "extmod/wasmmod/fetch.h"

#ifndef MICROPY_WASM_MALLOC
#define MICROPY_WASM_MALLOC(n) malloc(n)
#endif
#ifndef MICROPY_WASM_FREE
#define MICROPY_WASM_FREE(p) free(p)
#endif

bool mp_wasm_uri_is_http(const char *uri) {
    return uri != NULL
        && (memcmp(uri, "http://", 7) == 0 || memcmp(uri, "https://", 8) == 0);
}

void mp_wasm_join_uri(const char *root, const char *rel, vstr_t *out) {
    vstr_init(out, strlen(root) + strlen(rel) + 2);
    vstr_add_str(out, root);
    if (out->len > 0 && out->buf[out->len - 1] != '/') {
        vstr_add_char(out, '/');
    }
    if (rel[0] == '/') {
        rel++;
    }
    vstr_add_str(out, rel);
}

static bool fetch_file(const char *path, vstr_t *out) {
    mp_reader_t reader;
    mp_reader_new_file(&reader, qstr_from_str(path));
    vstr_init(out, 256);
    for (;;) {
        mp_uint_t b = reader.readbyte(reader.data);
        if (b == MP_READER_EOF) {
            break;
        }
        vstr_add_byte(out, (byte)b);
    }
    reader.close(reader.data);
    return true;
}

bool mp_wasm_fetch(const char *uri, vstr_t *out, char *errbuf, size_t errbuf_len) {
    if (uri == NULL || uri[0] == '\0') {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "empty uri");
        }
        return false;
    }

    #ifdef MICROPY_WASM_FETCH
    {
        uint8_t *hook_buf = NULL;
        uint32_t hook_len = 0;
        if (MICROPY_WASM_FETCH(uri, &hook_buf, &hook_len, errbuf, errbuf_len)) {
            vstr_init(out, hook_len + 1);
            vstr_add_strn(out, (const char *)hook_buf, hook_len);
            MICROPY_WASM_FREE(hook_buf);
            return true;
        }
    }
    #endif

    if (mp_wasm_uri_is_http(uri)) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "HTTP fetch requires MICROPY_WASM_FETCH");
        }
        return false;
    }

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        fetch_file(uri, out);
        nlr_pop();
        return true;
    }
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len, "fetch failed: %s", uri);
    }
    return false;
}

#endif // MICROPY_PY_WASM
