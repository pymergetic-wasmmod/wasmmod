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

/*
 * Browser HTTP I/O for wasmmod (js.fetch via Emscripten Asyncify).
 *
 * Wired by mpconfig_webassembly.h as MICROPY_WASM_IO_OPS.
 * Compiled when WASMMOD_EMSCRIPTEN=1 (see ../micropython.mk).
 *
 * Host clang/clangd: no emscripten.h — keep declarations only so the TU
 * analyzes cleanly; EM_ASYNC_JS bodies are JS and are not valid C for clang.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../../io.h"

/* Avoid fetch.h / cdn.h (pull py/mpconfig) for host clangd. */
bool mp_wasm_uri_is_http(const char *uri);
const char *mp_wasm_fetch_auth_bearer(void);
const char *mp_wasm_cdn_session_id(void);

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// Returns 0 on OK, -1 on failure. Fills *out_ptr / *out_len with malloc'd bytes.
// auth / session_id may be empty strings (never NULL from C callers).
EM_ASYNC_JS(int, mp_wasm_js_http_get, (const char *uri, const char *auth, const char *session_id,
                 uint8_t * *out_ptr, uint32_t * out_len), {
    try {
        const url = UTF8ToString(uri);
        const headers = {};
        const tok = UTF8ToString(auth);
        if (tok) {
            headers["Authorization"] = "Bearer " + tok;
        }
        const sid = UTF8ToString(session_id);
        if (sid) {
            headers["X-Shell-Session-Id"] = sid;
        }
        const resp = await fetch(url, {
            headers : headers,
            credentials : "same-origin",
            // Pin artifacts are Cache-Control: immutable — a pre-sign publish can
            // stick in the browser for a day and fail VERIFY. Always bypass.
            cache : "no-store",
        });
        if (!resp.ok) {
            console.warn("[wasmmod fetch]", resp.status, url);
            return -1;
        }
        const ab = await resp.arrayBuffer();
        const u8 = new Uint8Array(ab);
        const n = u8.length;
        const ptr = _malloc(n > 0 ? n : 1);
        if (!ptr) {
            return -1;
        }
        HEAPU8.set(u8, ptr);
        setValue(out_ptr, ptr, "*");
        setValue(out_len, n, "i32");
        return 0;
    } catch (e) {
        return -1;
    }
});

// Returns 0 if resource exists, -1 otherwise. Falls back to GET when HEAD fails.
EM_ASYNC_JS(int, mp_wasm_js_http_probe, (const char *uri, const char *auth, const char *session_id), {
    try {
        const url = UTF8ToString(uri);
        const headers = {};
        const tok = UTF8ToString(auth);
        if (tok) {
            headers["Authorization"] = "Bearer " + tok;
        }
        const sid = UTF8ToString(session_id);
        if (sid) {
            headers["X-Shell-Session-Id"] = sid;
        }
        const opts = {
            method : "HEAD",
            headers : headers,
            credentials : "same-origin",
            cache : "no-store",
        };
        let resp = await fetch(url, opts);
        if (resp.ok) {
            return 0;
        }
        // Some CDNs reject HEAD — try a cheap GET.
        if (resp.status === 405 || resp.status === 501 || resp.status === 403) {
            resp = await fetch(url, {
                method : "GET",
                headers : headers,
                credentials : "same-origin",
                cache : "no-store",
            });
            return resp.ok ? 0 : -1;
        }
        return -1;
    } catch (e) {
        return -1;
    }
});
#else
int mp_wasm_js_http_get(const char *uri, const char *auth, const char *session_id,
    uint8_t **out_ptr, uint32_t *out_len);
int mp_wasm_js_http_probe(const char *uri, const char *auth, const char *session_id);
#endif

static const char *browser_auth(void) {
    const char *t = mp_wasm_fetch_auth_bearer();
    return (t != NULL) ? t : "";
}

static const char *browser_session(void) {
    const char *s = mp_wasm_cdn_session_id();
    return (s != NULL) ? s : "";
}

static mp_wasm_io_result_t browser_fetch(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    if (!mp_wasm_uri_is_http(uri)) {
        return MP_WASM_IO_DECLINE;
    }
    uint8_t *buf = NULL;
    uint32_t len = 0;
    if (mp_wasm_js_http_get(uri, browser_auth(), browser_session(), &buf, &len) != 0) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "js.fetch failed: %s", uri);
        }
        return MP_WASM_IO_ERR;
    }
    *out_bytes = buf;
    *out_len = len;
    return MP_WASM_IO_OK;
}

static mp_wasm_io_result_t browser_probe(const char *uri) {
    if (!mp_wasm_uri_is_http(uri)) {
        return MP_WASM_IO_DECLINE;
    }
    return mp_wasm_js_http_probe(uri, browser_auth(), browser_session()) == 0
        ? MP_WASM_IO_OK
        : MP_WASM_IO_ERR;
}

const mp_wasm_io_ops_t mp_wasm_io_browser = {
    .version = MP_WASM_IO_OPS_VERSION,
    .fetch = browser_fetch,
    .probe = browser_probe,
    .yield = NULL,
    .request = NULL,
    .put = NULL,
    .userdata = NULL,
};
