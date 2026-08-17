/*
 * Browser HTTP I/O for wasmmod (js.fetch via Emscripten Asyncify).
 *
 * Port fill — not a card. Do not edit ports/webassembly/library.js.
 * WAN HTTP is js.fetch; a kernel's simulated L2 stays packets.
 *
 * Host clangd: no emscripten.h — declarations only; EM_ASYNC_JS is JS.
 */
#include "ports/webassembly/io_browser.h"

#include "pymergetic/wasmmod/io/__exports__.h"
#include "pymergetic/wasmmod/pack/alloc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(__EMSCRIPTEN__)
#include <stdlib.h>
#endif

const char *pm_wasmmod_net_cdn_session_id(void) __attribute__((weak));

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

EM_ASYNC_JS(int, pm_wasmmod_js_http_get,
    (const char *uri, const char *auth, const char *session_id, uint8_t **out_ptr, uint32_t *out_len), {
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
            cache : "no-store",
        });
        if (!resp.ok) {
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

EM_ASYNC_JS(int, pm_wasmmod_js_http_probe,
    (const char *uri, const char *auth, const char *session_id), {
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
int pm_wasmmod_js_http_get(const char *uri, const char *auth, const char *session_id,
    uint8_t **out_ptr, uint32_t *out_len);
int pm_wasmmod_js_http_probe(const char *uri, const char *auth, const char *session_id);
#endif

static const char *browser_auth(void) {
    const char *t = pm_wasmmod_io_auth_bearer();
    return (t != NULL) ? t : "";
}

static const char *browser_session(void) {
    const char *s = pm_wasmmod_net_cdn_session_id ? pm_wasmmod_net_cdn_session_id() : NULL;
    return (s != NULL) ? s : "";
}

static int browser_uri(const char *uri) {
    return pm_wasmmod_io_uri_is_http(uri) || (uri != NULL && memcmp(uri, "data:", 5) == 0);
}

static pm_wasmmod_io_result_t browser_fetch(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    uint8_t *jsbuf = NULL;
    uint32_t n = 0;
    uint8_t *p;
    if (!browser_uri(uri)) {
        return PM_WASMMOD_IO_DECLINE;
    }
    if (pm_wasmmod_js_http_get(uri, browser_auth(), browser_session(), &jsbuf, &n) != 0) {
        if (errbuf != NULL && errbuf_len > 0) {
            snprintf(errbuf, errbuf_len, "js.fetch failed: %s", uri);
        }
        return PM_WASMMOD_IO_ERR;
    }
    p = (uint8_t *)MICROPY_WASM_MALLOC(n > 0 ? n : 1u);
    if (p == NULL) {
#if defined(__EMSCRIPTEN__)
        free(jsbuf);
#endif
        return PM_WASMMOD_IO_ERR;
    }
    if (n > 0 && jsbuf != NULL) {
        memcpy(p, jsbuf, n);
    }
#if defined(__EMSCRIPTEN__)
    free(jsbuf);
#endif
    *out_bytes = p;
    *out_len = n;
    return PM_WASMMOD_IO_OK;
}

static pm_wasmmod_io_result_t browser_probe(const char *uri) {
    if (!browser_uri(uri)) {
        return PM_WASMMOD_IO_DECLINE;
    }
    return pm_wasmmod_js_http_probe(uri, browser_auth(), browser_session()) == 0
        ? PM_WASMMOD_IO_OK
        : PM_WASMMOD_IO_ERR;
}

const pm_wasmmod_io_ops_t pm_wasmmod_io_browser = {
    .version = PM_WASMMOD_IO_OPS_VERSION,
    .fetch = browser_fetch,
    .probe = browser_probe,
    .yield = NULL,
    .request = NULL,
    .put = NULL,
    .userdata = NULL,
};
