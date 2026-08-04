/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Public API is host-agnostic (cdn.h). MicroPython fetch/finder used only
 * as the current I/O backend inside this translation unit.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif
#ifndef MICROPY_PY_WASM_AOT
#define MICROPY_PY_WASM_AOT (0)
#endif

#if MICROPY_PY_WASM

#include <stdio.h>
#include <string.h>

#include "extmod/wasmmod/cdn.h"
#include "extmod/wasmmod/fetch.h"
#include "extmod/wasmmod/finder.h"
#include "extmod/wasmmod/alloc.h" // IWYU pragma: keep — MICROPY_WASM_MALLOC
#include "extmod/wasmmod/zlibutil.h"

#ifndef MP_WASM_CDN_BASE_MAX
#define MP_WASM_CDN_BASE_MAX (256)
#endif
#ifndef MP_WASM_CDN_TOKEN_MAX
#define MP_WASM_CDN_TOKEN_MAX (256)
#endif

static mp_wasm_cdn_driver_t g_driver = MP_WASM_CDN_DRIVER_PATH;
static char g_base[MP_WASM_CDN_BASE_MAX];
static char g_token[MP_WASM_CDN_TOKEN_MAX];

static bool looks_like_cdn_base(const char *url) {
    if (url == NULL) {
        return false;
    }
    size_t n = strlen(url);
    while (n > 0 && url[n - 1] == '/') {
        n--;
    }
    if (n >= 4 && memcmp(url + n - 4, "/cdn", 4) == 0) {
        return true;
    }
    if (n >= 3 && memcmp(url + n - 3, "cdn", 3) == 0) {
        return true;
    }
    return strstr(url, "/cdn/") != NULL;
}

void mp_wasm_cdn_reset(void) {
    g_driver = MP_WASM_CDN_DRIVER_PATH;
    g_base[0] = '\0';
    g_token[0] = '\0';
    mp_wasm_fetch_set_auth_bearer(NULL);
}

void mp_wasm_cdn_configure(const char *base_url, const char *token) {
    g_base[0] = '\0';
    g_token[0] = '\0';
    if (base_url != NULL && base_url[0] != '\0') {
        size_t n = strlen(base_url);
        while (n > 0 && base_url[n - 1] == '/') {
            n--;
        }
        if (n >= MP_WASM_CDN_BASE_MAX) {
            n = MP_WASM_CDN_BASE_MAX - 1;
        }
        memcpy(g_base, base_url, n);
        g_base[n] = '\0';
        g_driver = looks_like_cdn_base(g_base) ? MP_WASM_CDN_DRIVER_METAL : MP_WASM_CDN_DRIVER_PATH;
    } else {
        g_driver = MP_WASM_CDN_DRIVER_PATH;
    }
    if (token != NULL && token[0] != '\0') {
        size_t n = strlen(token);
        if (n >= MP_WASM_CDN_TOKEN_MAX) {
            n = MP_WASM_CDN_TOKEN_MAX - 1;
        }
        memcpy(g_token, token, n);
        g_token[n] = '\0';
        mp_wasm_fetch_set_auth_bearer(g_token);
    } else {
        mp_wasm_fetch_set_auth_bearer(NULL);
    }
}

mp_wasm_cdn_driver_t mp_wasm_cdn_driver(void) {
    return g_driver;
}

bool mp_wasm_cdn_require_explicit_deps(void) {
    return g_driver == MP_WASM_CDN_DRIVER_METAL;
}

const char *mp_wasm_cdn_driver_name(void) {
    return g_driver == MP_WASM_CDN_DRIVER_METAL ? "metal-cdn" : "path";
}

// Port glue: fetch into malloc'd buffer (upy vstr is internal only).
// Always return naked artifact bytes (unwrap MPZL when present).
static bool try_fetch_uri(const char *uri, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    vstr_t tmp;
    vstr_init(&tmp, 256);
    if (!mp_wasm_fetch(uri, &tmp, errbuf, errbuf_len)) {
        vstr_clear(&tmp);
        return false;
    }
    const uint8_t *p = (const uint8_t *)tmp.buf;
    uint32_t plen = (uint32_t)tmp.len;
    uint8_t *unwrapped = NULL;
    if (!mp_wasm_artifact_unwrap_zlib(&p, &plen, &unwrapped)) {
        vstr_clear(&tmp);
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "cdn: bad MPZL envelope");
        }
        return false;
    }
    if (unwrapped != NULL) {
        vstr_clear(&tmp);
        *out_bytes = unwrapped;
        *out_len = plen;
        return true;
    }
    uint8_t *buf = MICROPY_WASM_MALLOC(tmp.len ? tmp.len : 1);
    if (buf == NULL) {
        vstr_clear(&tmp);
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "cdn: oom");
        }
        return false;
    }
    if (tmp.len) {
        memcpy(buf, tmp.buf, tmp.len);
    }
    *out_bytes = buf;
    *out_len = (uint32_t)tmp.len;
    vstr_clear(&tmp);
    return true;
}

static bool metal_fetch(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len, char *errbuf, size_t errbuf_len) {
    if (g_base[0] == '\0') {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "cdn: no base URL");
        }
        return false;
    }
    const char *ver = (version != NULL) ? version : "";

    // One host AOT format tag only (WAMR AOT_CURRENT_VERSION → ".aotN").
    // When AOT is enabled, prefer that artifact; else wasm. Always try .zlib first.
    const char *exts[5];
    unsigned n = 0;
    #if MICROPY_PY_WASM_AOT
    char aot_ext[16];
    char aot_zlib[24];
    mp_wasm_aot_format_ext(aot_ext, sizeof(aot_ext));
    snprintf(aot_zlib, sizeof(aot_zlib), "%s.zlib", aot_ext);
    exts[n++] = aot_zlib;
    exts[n++] = aot_ext;
    #endif
    exts[n++] = ".wasm.zlib";
    exts[n++] = ".wasm";
    exts[n] = NULL;

    char uri[MP_WASM_CDN_BASE_MAX + 128];
    if (ver[0] != '\0') {
        for (const char **e = exts; *e != NULL; ++e) {
            snprintf(uri, sizeof(uri), "%s/artifacts/pin/%s/%s%s", g_base, ver, name, *e);
            if (try_fetch_uri(uri, out_bytes, out_len, errbuf, errbuf_len)) {
                return true;
            }
        }
    }
    for (const char **e = exts; *e != NULL; ++e) {
        snprintf(uri, sizeof(uri), "%s/artifacts/lead/%s%s", g_base, name, *e);
        if (try_fetch_uri(uri, out_bytes, out_len, errbuf, errbuf_len)) {
            return true;
        }
    }
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len, "cdn: package %s@%s not found", name, ver);
    }
    return false;
}

static bool path_fetch(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len, char *errbuf, size_t errbuf_len) {
    vstr_t path;
    vstr_init(&path, 64);
    if (version != NULL && version[0] != '\0') {
        char stem[MP_WASM_CDN_BASE_MAX];
        snprintf(stem, sizeof(stem), "%s@%s", name, version);
        if (mp_wasm_find_pack(stem, &path)) {
            bool ok = try_fetch_uri(vstr_null_terminated_str(&path), out_bytes, out_len, errbuf, errbuf_len);
            vstr_clear(&path);
            if (ok) {
                return true;
            }
            vstr_init(&path, 64);
        }
    }
    if (mp_wasm_find_pack(name, &path)) {
        bool ok = try_fetch_uri(vstr_null_terminated_str(&path), out_bytes, out_len, errbuf, errbuf_len);
        vstr_clear(&path);
        return ok;
    }
    vstr_clear(&path);
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len, "path: pack %s not found", name);
    }
    return false;
}

bool mp_wasm_cdn_fetch_pack(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    if (out_bytes == NULL || out_len == NULL) {
        return false;
    }
    *out_bytes = NULL;
    *out_len = 0;
    if (name == NULL || name[0] == '\0') {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "cdn: empty name");
        }
        return false;
    }
    if (g_driver == MP_WASM_CDN_DRIVER_METAL) {
        return metal_fetch(name, version, out_bytes, out_len, errbuf, errbuf_len);
    }
    return path_fetch(name, version, out_bytes, out_len, errbuf, errbuf_len);
}

#endif // MICROPY_PY_WASM
