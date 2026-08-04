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
#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#if MICROPY_PY_WASM

#include <stdio.h>
#include <string.h>

#include "py/runtime.h"

#include "extmod/wasmmod/cdn.h"
#include "extmod/wasmmod/fetch.h"
#include "extmod/wasmmod/finder.h"
#include "extmod/wasmmod/alloc.h" // IWYU pragma: keep — MICROPY_WASM_MALLOC
#include "extmod/wasmmod/zlibutil.h"
#include "extmod/wasmmod/io.h"

#ifndef MP_WASM_CDN_BASE_MAX
#define MP_WASM_CDN_BASE_MAX (256)
#endif
#ifndef MP_WASM_CDN_TOKEN_MAX
#define MP_WASM_CDN_TOKEN_MAX (256)
#endif

static mp_wasm_cdn_driver_t g_driver = MP_WASM_CDN_DRIVER_PATH;
static char g_base[MP_WASM_CDN_BASE_MAX];
static char g_token[MP_WASM_CDN_TOKEN_MAX];
#ifndef MP_WASM_CDN_SESSION_ID_MAX
#define MP_WASM_CDN_SESSION_ID_MAX (64)
#endif
static char g_session_id[MP_WASM_CDN_SESSION_ID_MAX];

static size_t url_len_no_slash(const char *url) {
    size_t n = strlen(url);
    while (n > 0 && url[n - 1] == '/') {
        n--;
    }
    return n;
}

bool mp_wasm_cdn_url_is_base(const char *url) {
    if (url == NULL || g_base[0] == '\0') {
        return false;
    }
    size_t un = url_len_no_slash(url);
    size_t bn = url_len_no_slash(g_base);
    return un == bn && un > 0 && strncmp(url, g_base, bn) == 0;
}

void mp_wasm_cdn_reset(void) {
    g_driver = MP_WASM_CDN_DRIVER_PATH;
    g_base[0] = '\0';
    g_token[0] = '\0';
    g_session_id[0] = '\0';
    mp_wasm_fetch_set_auth_bearer(NULL);
}

void mp_wasm_cdn_configure(const char *base_url, const char *token) {
    g_base[0] = '\0';
    g_token[0] = '\0';
    if (base_url != NULL && base_url[0] != '\0') {
        size_t n = url_len_no_slash(base_url);
        if (n >= MP_WASM_CDN_BASE_MAX) {
            n = MP_WASM_CDN_BASE_MAX - 1;
        }
        memcpy(g_base, base_url, n);
        g_base[n] = '\0';
        // Explicit bind via wasm.cdn() — always metal-cdn (artifacts/index).
        g_driver = MP_WASM_CDN_DRIVER_METAL;
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

const char *mp_wasm_cdn_base(void) {
    return g_base[0] != '\0' ? g_base : NULL;
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
    } else {
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
    }
    // Reject non-artifact HTTP 200s (HTML/JSON SPA fallbacks) before verify.
    {
        const uint8_t *b = *out_bytes;
        uint32_t n = *out_len;
        bool ok_magic = n >= 4
            && ((b[0] == 0x00 && b[1] == 'a' && b[2] == 's' && b[3] == 'm')
                || (b[0] == 0x00 && b[1] == 'a' && b[2] == 'o' && b[3] == 't')
                || (b[0] == 0x7f && b[1] == 'E' && b[2] == 'L' && b[3] == 'F'));
        if (!ok_magic) {
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len,
                    "cdn: not a wasm/aot/elf artifact (len=%u magic=%02x%02x%02x%02x)",
                    (unsigned)n,
                    n > 0 ? b[0] : 0, n > 1 ? b[1] : 0,
                    n > 2 ? b[2] : 0, n > 3 ? b[3] : 0);
            }
            MICROPY_WASM_FREE(*out_bytes);
            *out_bytes = NULL;
            *out_len = 0;
            return false;
        }
    }
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

    // Probe order follows MICROPY_WASM_CONTAINERS (zlib preferred per try_fetch path).
    // ELF mirrors finder try_elf_variants: name.<arch>.elf(.zlib) then naked .elf.
    const char *exts[24];
    unsigned n = 0;
#ifndef MICROPY_WASM_CONTAINERS
#define MICROPY_WASM_CONTAINERS "elf,aot,wasm"
#endif
#if MICROPY_PY_WASM_AOT
    char aot_ext[16];
    char aot_zlib[24];
    mp_wasm_aot_format_ext(aot_ext, sizeof(aot_ext));
    snprintf(aot_zlib, sizeof(aot_zlib), "%s.zlib", aot_ext);
#endif
#if MICROPY_PY_WASM_ELF
    // Storage for ".<arch>.elf" / ".<arch>.elf.zlib" (arch tags from wasm.arch).
    char elf_arch_ext[8][48];
    char elf_arch_zlib[8][56];
    unsigned n_elf_arch = 0;
#endif
    {
        const char *pref = MICROPY_WASM_CONTAINERS;
        const char *p = pref;
        while (*p && n + 2 < 24) {
            while (*p == ',' || *p == ' ') {
                p++;
            }
            if (!*p) {
                break;
            }
            const char *start = p;
            while (*p && *p != ',' && *p != ' ') {
                p++;
            }
            size_t kn = (size_t)(p - start);
#if MICROPY_PY_WASM_ELF
            if (kn == 3 && memcmp(start, "elf", 3) == 0) {
                mp_wasm_arch_ensure();
                size_t n_arch = 0;
                mp_obj_t *arch_items = NULL;
                mp_obj_list_get(mp_wasm_arch_obj(), &n_arch, &arch_items);
                for (size_t ai = 0; ai < n_arch && n_elf_arch < 8 && n + 2 < 24; ++ai) {
                    if (!mp_obj_is_str(arch_items[ai])) {
                        continue;
                    }
                    const char *arch = mp_obj_str_get_str(arch_items[ai]);
                    if (arch == NULL || arch[0] == '\0') {
                        continue;
                    }
                    snprintf(elf_arch_ext[n_elf_arch], sizeof(elf_arch_ext[0]), ".%s.elf", arch);
                    snprintf(elf_arch_zlib[n_elf_arch], sizeof(elf_arch_zlib[0]), ".%s.elf.zlib", arch);
                    exts[n++] = elf_arch_zlib[n_elf_arch];
                    exts[n++] = elf_arch_ext[n_elf_arch];
                    n_elf_arch++;
                }
                if (n + 2 < 24) {
                    exts[n++] = ".elf.zlib";
                    exts[n++] = ".elf";
                }
                continue;
            }
#endif
#if MICROPY_PY_WASM_AOT
            if (kn == 3 && memcmp(start, "aot", 3) == 0) {
                exts[n++] = aot_zlib;
                exts[n++] = aot_ext;
                continue;
            }
#endif
            if (kn == 4 && memcmp(start, "wasm", 4) == 0) {
                exts[n++] = ".wasm.zlib";
                exts[n++] = ".wasm";
            }
        }
    }
    if (n == 0) {
        exts[n++] = ".wasm.zlib";
        exts[n++] = ".wasm";
    }
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

void mp_wasm_cdn_set_session_id(const char *session_id) {
    g_session_id[0] = '\0';
    if (session_id == NULL || session_id[0] == '\0') {
        return;
    }
    size_t n = strlen(session_id);
    if (n >= MP_WASM_CDN_SESSION_ID_MAX) {
        n = MP_WASM_CDN_SESSION_ID_MAX - 1;
    }
    memcpy(g_session_id, session_id, n);
    g_session_id[n] = '\0';
}

const char *mp_wasm_cdn_session_id(void) {
    return g_session_id;
}

bool mp_wasm_cdn_fetch_index(const char *channel,
    uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    if (out_bytes == NULL || out_len == NULL) {
        return false;
    }
    *out_bytes = NULL;
    *out_len = 0;
    if (g_base[0] == '\0' || g_driver != MP_WASM_CDN_DRIVER_METAL) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "cdn: configure metal-cdn base first (wasm.cdn)");
        }
        return false;
    }
    const char *ch = (channel != NULL && channel[0] != '\0') ? channel : "lead";
    char uri[MP_WASM_CDN_BASE_MAX + 96];
    if (ch[0] == '@') {
        snprintf(uri, sizeof(uri), "%s/index/pin/%s", g_base, ch + 1);
    } else if (strncmp(ch, "pin/", 4) == 0) {
        snprintf(uri, sizeof(uri), "%s/index/pin/%s", g_base, ch + 4);
    } else {
        snprintf(uri, sizeof(uri), "%s/index/%s", g_base, ch);
    }
    vstr_t tmp;
    vstr_init(&tmp, 256);
    if (!mp_wasm_fetch(uri, &tmp, errbuf, errbuf_len)) {
        vstr_clear(&tmp);
        return false;
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

bool mp_wasm_cdn_publish(const char *name, const char *version,
    const uint8_t *data, uint32_t data_len,
    bool lead, bool pin, const char *token,
    char *errbuf, size_t errbuf_len) {
    (void)name;
    (void)version;
    (void)data;
    (void)data_len;
    (void)lead;
    (void)pin;
    (void)token;
    const mp_wasm_io_ops_t *ops = mp_wasm_io_get();
    if (ops != NULL && ops->request != NULL) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "cdn: publish request op not fully wired");
        }
        return false;
    }
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len, "cdn: publish not supported on this host yet");
    }
    return false;
}

#endif // MICROPY_PY_WASM
