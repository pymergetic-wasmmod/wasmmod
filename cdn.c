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

#if MICROPY_PY_WASM_ELF || MICROPY_PY_WASM_AOT
#include "py/objlist.h" // mp_obj_list_get (wasm.arch probing)
#endif

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
static char g_bases[MP_WASM_CDN_BASES_MAX][MP_WASM_CDN_BASE_MAX];
static unsigned g_n_bases;
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

static void set_token(const char *token) {
    g_token[0] = '\0';
    if (token != NULL && token[0] != '\0') {
        size_t n = strlen(token);
        if (n >= MP_WASM_CDN_TOKEN_MAX) {
            n = MP_WASM_CDN_TOKEN_MAX - 1;
        }
        memcpy(g_token, token, n);
        g_token[n] = '\0';
        mp_wasm_fetch_set_auth_bearer(g_token);
    } else if (g_n_bases == 0) {
        mp_wasm_fetch_set_auth_bearer(NULL);
    }
}

static bool store_base_at(unsigned idx, const char *base_url) {
    size_t n;
    if (idx >= MP_WASM_CDN_BASES_MAX || base_url == NULL || base_url[0] == '\0') {
        return false;
    }
    n = url_len_no_slash(base_url);
    if (n >= MP_WASM_CDN_BASE_MAX) {
        n = MP_WASM_CDN_BASE_MAX - 1;
    }
    memcpy(g_bases[idx], base_url, n);
    g_bases[idx][n] = '\0';
    return true;
}

static bool already_have(const char *base_url) {
    size_t un;
    unsigned i;
    if (base_url == NULL || base_url[0] == '\0') {
        return false;
    }
    un = url_len_no_slash(base_url);
    for (i = 0; i < g_n_bases; ++i) {
        size_t bn = url_len_no_slash(g_bases[i]);
        if (un == bn && un > 0 && strncmp(base_url, g_bases[i], bn) == 0) {
            return true;
        }
    }
    return false;
}

bool mp_wasm_cdn_url_is_base(const char *url) {
    size_t un;
    unsigned i;
    if (url == NULL || g_n_bases == 0) {
        return false;
    }
    un = url_len_no_slash(url);
    for (i = 0; i < g_n_bases; ++i) {
        size_t bn = url_len_no_slash(g_bases[i]);
        if (un == bn && un > 0 && strncmp(url, g_bases[i], bn) == 0) {
            return true;
        }
    }
    return false;
}

void mp_wasm_cdn_reset(void) {
    unsigned i;
    g_driver = MP_WASM_CDN_DRIVER_PATH;
    g_n_bases = 0;
    for (i = 0; i < MP_WASM_CDN_BASES_MAX; ++i) {
        g_bases[i][0] = '\0';
    }
    g_token[0] = '\0';
    g_session_id[0] = '\0';
    mp_wasm_fetch_set_auth_bearer(NULL);
}

void mp_wasm_cdn_configure(const char *base_url, const char *token) {
    unsigned i;
    g_n_bases = 0;
    for (i = 0; i < MP_WASM_CDN_BASES_MAX; ++i) {
        g_bases[i][0] = '\0';
    }
    g_driver = MP_WASM_CDN_DRIVER_PATH;
    g_token[0] = '\0';
    mp_wasm_fetch_set_auth_bearer(NULL);
    if (base_url != NULL && base_url[0] != '\0' && store_base_at(0, base_url)) {
        g_n_bases = 1;
        g_driver = MP_WASM_CDN_DRIVER_METAL;
    }
    set_token(token);
}

bool mp_wasm_cdn_add(const char *base_url, const char *token) {
    if (base_url == NULL || base_url[0] == '\0' || already_have(base_url)) {
        return false;
    }
    if (g_n_bases >= MP_WASM_CDN_BASES_MAX) {
        return false;
    }
    if (!store_base_at(g_n_bases, base_url)) {
        return false;
    }
    g_n_bases++;
    g_driver = MP_WASM_CDN_DRIVER_METAL;
    if (token != NULL && token[0] != '\0') {
        set_token(token);
    }
    return true;
}

bool mp_wasm_cdn_prepend(const char *base_url, const char *token) {
    unsigned i;
    if (base_url == NULL || base_url[0] == '\0' || already_have(base_url)) {
        return false;
    }
    if (g_n_bases >= MP_WASM_CDN_BASES_MAX) {
        return false;
    }
    for (i = g_n_bases; i > 0; --i) {
        memcpy(g_bases[i], g_bases[i - 1], MP_WASM_CDN_BASE_MAX);
    }
    if (!store_base_at(0, base_url)) {
        return false;
    }
    g_n_bases++;
    g_driver = MP_WASM_CDN_DRIVER_METAL;
    if (token != NULL && token[0] != '\0') {
        set_token(token);
    }
    return true;
}

mp_wasm_cdn_driver_t mp_wasm_cdn_driver(void) {
    return g_driver;
}

const char *mp_wasm_cdn_base(void) {
    return g_n_bases > 0 ? g_bases[0] : NULL;
}

unsigned mp_wasm_cdn_base_count(void) {
    return g_n_bases;
}

const char *mp_wasm_cdn_base_at(unsigned index) {
    return index < g_n_bases ? g_bases[index] : NULL;
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

static bool metal_fetch_one(const char *base, const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len,
    char *out_origin, size_t origin_len,
    char *errbuf, size_t errbuf_len);

static bool metal_fetch(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len,
    char *out_origin, size_t origin_len,
    char *errbuf, size_t errbuf_len) {
    unsigned bi;
    if (g_n_bases == 0) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "cdn: no base URL");
        }
        return false;
    }
    for (bi = 0; bi < g_n_bases; ++bi) {
        if (metal_fetch_one(g_bases[bi], name, version, out_bytes, out_len,
                out_origin, origin_len, errbuf, errbuf_len)) {
            return true;
        }
    }
    if (errbuf && errbuf_len) {
        const char *ver = (version != NULL) ? version : "";
        snprintf(errbuf, errbuf_len, "cdn: package %s@%s not found", name, ver);
    }
    return false;
}

static bool metal_fetch_one(const char *base, const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len,
    char *out_origin, size_t origin_len,
    char *errbuf, size_t errbuf_len) {
    if (base == NULL || base[0] == '\0') {
        return false;
    }
    const char *ver = (version != NULL) ? version : "";

    // Probe order follows MICROPY_WASM_CONTAINERS (zlib preferred per try_fetch path).
    // ELF/AOT mirror finder: name.<arch>.ext(.zlib) then naked ext (+ legacy .aot).
    const char *exts[32];
    unsigned n = 0;
#ifndef MICROPY_WASM_CONTAINERS
#define MICROPY_WASM_CONTAINERS "elf,aot,wasm"
#endif
#if MICROPY_PY_WASM_AOT
    char aot_ext[16];
    char aot_zlib[24];
    mp_wasm_aot_format_ext(aot_ext, sizeof(aot_ext));
    snprintf(aot_zlib, sizeof(aot_zlib), "%s.zlib", aot_ext);
    // ".<arch>.aotN" / ".<arch>.aotN.zlib" (+ legacy ".aot" twins).
    char aot_arch_ext[8][48];
    char aot_arch_zlib[8][56];
    char aot_leg_arch_ext[8][40];
    char aot_leg_arch_zlib[8][48];
    unsigned n_aot_arch = 0;
    unsigned n_aot_leg_arch = 0;
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
        while (*p && n + 2 < 32) {
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
                for (size_t ai = 0; ai < n_arch && n_elf_arch < 8 && n + 2 < 32; ++ai) {
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
                if (n + 2 < 32) {
                    exts[n++] = ".elf.zlib";
                    exts[n++] = ".elf";
                }
                continue;
            }
#endif
#if MICROPY_PY_WASM_AOT
            if (kn == 3 && memcmp(start, "aot", 3) == 0) {
                mp_wasm_arch_ensure();
                size_t n_arch = 0;
                mp_obj_t *arch_items = NULL;
                mp_obj_list_get(mp_wasm_arch_obj(), &n_arch, &arch_items);
                for (size_t ai = 0; ai < n_arch && n_aot_arch < 8 && n + 2 < 32; ++ai) {
                    if (!mp_obj_is_str(arch_items[ai])) {
                        continue;
                    }
                    const char *arch = mp_obj_str_get_str(arch_items[ai]);
                    if (arch == NULL || arch[0] == '\0') {
                        continue;
                    }
                    snprintf(aot_arch_ext[n_aot_arch], sizeof(aot_arch_ext[0]), ".%s%s", arch, aot_ext);
                    snprintf(aot_arch_zlib[n_aot_arch], sizeof(aot_arch_zlib[0]), ".%s%s.zlib", arch, aot_ext);
                    exts[n++] = aot_arch_zlib[n_aot_arch];
                    exts[n++] = aot_arch_ext[n_aot_arch];
                    n_aot_arch++;
                }
                if (n + 2 < 32) {
                    exts[n++] = aot_zlib;
                    exts[n++] = aot_ext;
                }
                // Legacy unversioned .aot after format-tagged name (finder parity).
                if (strcmp(aot_ext, ".aot") != 0) {
                    for (size_t ai = 0; ai < n_arch && n_aot_leg_arch < 8 && n + 2 < 32; ++ai) {
                        if (!mp_obj_is_str(arch_items[ai])) {
                            continue;
                        }
                        const char *arch = mp_obj_str_get_str(arch_items[ai]);
                        if (arch == NULL || arch[0] == '\0') {
                            continue;
                        }
                        snprintf(aot_leg_arch_ext[n_aot_leg_arch], sizeof(aot_leg_arch_ext[0]),
                            ".%s.aot", arch);
                        snprintf(aot_leg_arch_zlib[n_aot_leg_arch], sizeof(aot_leg_arch_zlib[0]),
                            ".%s.aot.zlib", arch);
                        exts[n++] = aot_leg_arch_zlib[n_aot_leg_arch];
                        exts[n++] = aot_leg_arch_ext[n_aot_leg_arch];
                        n_aot_leg_arch++;
                    }
                    if (n + 2 < 32) {
                        exts[n++] = ".aot.zlib";
                        exts[n++] = ".aot";
                    }
                }
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
            snprintf(uri, sizeof(uri), "%s/artifacts/pin/%s/%s%s", base, ver, name, *e);
            if (try_fetch_uri(uri, out_bytes, out_len, errbuf, errbuf_len)) {
                if (out_origin != NULL && origin_len > 0) {
                    strncpy(out_origin, uri, origin_len - 1);
                    out_origin[origin_len - 1] = '\0';
                }
                return true;
            }
        }
    }
    for (const char **e = exts; *e != NULL; ++e) {
        snprintf(uri, sizeof(uri), "%s/artifacts/lead/%s%s", base, name, *e);
        if (try_fetch_uri(uri, out_bytes, out_len, errbuf, errbuf_len)) {
            if (out_origin != NULL && origin_len > 0) {
                strncpy(out_origin, uri, origin_len - 1);
                out_origin[origin_len - 1] = '\0';
            }
            return true;
        }
    }
    return false;
}

static bool path_fetch(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len,
    char *out_origin, size_t origin_len,
    char *errbuf, size_t errbuf_len) {
    vstr_t path;
    vstr_init(&path, 64);
    if (version != NULL && version[0] != '\0') {
        char stem[MP_WASM_CDN_BASE_MAX];
        snprintf(stem, sizeof(stem), "%s@%s", name, version);
        if (mp_wasm_find_pack(stem, &path)) {
            const char *p = vstr_null_terminated_str(&path);
            bool ok = try_fetch_uri(p, out_bytes, out_len, errbuf, errbuf_len);
            if (ok && out_origin != NULL && origin_len > 0) {
                strncpy(out_origin, p, origin_len - 1);
                out_origin[origin_len - 1] = '\0';
            }
            vstr_clear(&path);
            if (ok) {
                return true;
            }
            vstr_init(&path, 64);
        }
    }
    if (mp_wasm_find_pack(name, &path)) {
        const char *p = vstr_null_terminated_str(&path);
        bool ok = try_fetch_uri(p, out_bytes, out_len, errbuf, errbuf_len);
        if (ok && out_origin != NULL && origin_len > 0) {
            strncpy(out_origin, p, origin_len - 1);
            out_origin[origin_len - 1] = '\0';
        }
        vstr_clear(&path);
        return ok;
    }
    vstr_clear(&path);
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len, "path: pack %s not found", name);
    }
    return false;
}

bool mp_wasm_cdn_fetch_pack_ex(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len,
    char *out_origin, size_t origin_len,
    char *errbuf, size_t errbuf_len) {
    if (out_bytes == NULL || out_len == NULL) {
        return false;
    }
    *out_bytes = NULL;
    *out_len = 0;
    if (out_origin != NULL && origin_len > 0) {
        out_origin[0] = '\0';
    }
    if (name == NULL || name[0] == '\0') {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "cdn: empty name");
        }
        return false;
    }
    if (g_driver == MP_WASM_CDN_DRIVER_METAL) {
        return metal_fetch(name, version, out_bytes, out_len, out_origin, origin_len, errbuf, errbuf_len);
    }
    return path_fetch(name, version, out_bytes, out_len, out_origin, origin_len, errbuf, errbuf_len);
}

bool mp_wasm_cdn_fetch_pack(const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    return mp_wasm_cdn_fetch_pack_ex(name, version, out_bytes, out_len, NULL, 0, errbuf, errbuf_len);
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
    if (g_n_bases == 0 || g_driver != MP_WASM_CDN_DRIVER_METAL) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "cdn: configure metal-cdn base first (wasm.cdn)");
        }
        return false;
    }
    {
        const char *ch = (channel != NULL && channel[0] != '\0') ? channel : "lead";
        char uri[MP_WASM_CDN_BASE_MAX + 96];
        unsigned bi;
        for (bi = 0; bi < g_n_bases; ++bi) {
            const char *base = g_bases[bi];
            vstr_t tmp;
            if (ch[0] == '@') {
                snprintf(uri, sizeof(uri), "%s/index/pin/%s", base, ch + 1);
            } else if (strncmp(ch, "pin/", 4) == 0) {
                snprintf(uri, sizeof(uri), "%s/index/pin/%s", base, ch + 4);
            } else {
                snprintf(uri, sizeof(uri), "%s/index/%s", base, ch);
            }
            vstr_init(&tmp, 256);
            if (!mp_wasm_fetch(uri, &tmp, errbuf, errbuf_len)) {
                vstr_clear(&tmp);
                continue;
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
        return false;
    }
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
