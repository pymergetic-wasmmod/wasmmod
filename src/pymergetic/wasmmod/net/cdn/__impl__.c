/* pymergetic.wasmmod.net.cdn — impl. Artifact CDN client on io_ops. No sockets. */
#include "pymergetic/wasmmod/net/cdn/__types__.h"
#include "pymergetic/wasmmod/io/__types__.h"

#include "pymergetic/wasmmod/pack/alloc.h"

#include <stdio.h>
#include <string.h>

#ifndef PM_WASMMOD_NET_CDN_BASE_MAX
#define PM_WASMMOD_NET_CDN_BASE_MAX (256)
#endif
#ifndef PM_WASMMOD_NET_CDN_TOKEN_MAX
#define PM_WASMMOD_NET_CDN_TOKEN_MAX (256)
#endif
#ifndef PM_WASMMOD_NET_CDN_SESSION_ID_MAX
#define PM_WASMMOD_NET_CDN_SESSION_ID_MAX (64)
#endif

#ifndef MICROPY_WASM_CONTAINERS
#define MICROPY_WASM_CONTAINERS "elf,aot,wasm"
#endif
#ifndef MICROPY_WASM_AOT_VERSION
#define MICROPY_WASM_AOT_VERSION (0)
#endif

static pm_wasmmod_net_cdn_driver_t g_driver = PM_WASMMOD_NET_CDN_DRIVER_PATH;
static char g_bases[PM_WASMMOD_NET_CDN_BASES_MAX][PM_WASMMOD_NET_CDN_BASE_MAX];
static unsigned g_n_bases;
static char g_token[PM_WASMMOD_NET_CDN_TOKEN_MAX];
static char g_session_id[PM_WASMMOD_NET_CDN_SESSION_ID_MAX];

static void err_set(char *errbuf, size_t errbuf_len, const char *msg) {
    if (errbuf == NULL || errbuf_len == 0) {
        return;
    }
    snprintf(errbuf, errbuf_len, "%s", msg);
}

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
        if (n >= PM_WASMMOD_NET_CDN_TOKEN_MAX) {
            n = PM_WASMMOD_NET_CDN_TOKEN_MAX - 1;
        }
        memcpy(g_token, token, n);
        g_token[n] = '\0';
        pm_wasmmod_io_set_auth_bearer(g_token);
    } else if (g_n_bases == 0) {
        pm_wasmmod_io_set_auth_bearer(NULL);
    }
}

static int32_t store_base_at(unsigned idx, const char *base_url) {
    if (idx >= PM_WASMMOD_NET_CDN_BASES_MAX || base_url == NULL || base_url[0] == '\0') {
        return 0;
    }
    size_t n = url_len_no_slash(base_url);
    if (n >= PM_WASMMOD_NET_CDN_BASE_MAX) {
        n = PM_WASMMOD_NET_CDN_BASE_MAX - 1;
    }
    memcpy(g_bases[idx], base_url, n);
    g_bases[idx][n] = '\0';
    return 1;
}

static int32_t already_have(const char *base_url) {
    if (base_url == NULL || base_url[0] == '\0') {
        return 0;
    }
    size_t un = url_len_no_slash(base_url);
    for (unsigned i = 0; i < g_n_bases; ++i) {
        size_t bn = url_len_no_slash(g_bases[i]);
        if (un == bn && un > 0 && strncmp(base_url, g_bases[i], bn) == 0) {
            return 1;
        }
    }
    return 0;
}

int32_t pm_wasmmod_net_cdn_url_is_base(const char *url) {
    if (url == NULL || g_n_bases == 0) {
        return 0;
    }
    size_t un = url_len_no_slash(url);
    for (unsigned i = 0; i < g_n_bases; ++i) {
        size_t bn = url_len_no_slash(g_bases[i]);
        if (un == bn && un > 0 && strncmp(url, g_bases[i], bn) == 0) {
            return 1;
        }
    }
    return 0;
}

void pm_wasmmod_net_cdn_reset(void) {
    g_driver = PM_WASMMOD_NET_CDN_DRIVER_PATH;
    g_n_bases = 0;
    for (unsigned i = 0; i < PM_WASMMOD_NET_CDN_BASES_MAX; ++i) {
        g_bases[i][0] = '\0';
    }
    g_token[0] = '\0';
    g_session_id[0] = '\0';
    pm_wasmmod_io_set_auth_bearer(NULL);
}

void pm_wasmmod_net_cdn_configure(const char *base_url, const char *token) {
    g_n_bases = 0;
    for (unsigned i = 0; i < PM_WASMMOD_NET_CDN_BASES_MAX; ++i) {
        g_bases[i][0] = '\0';
    }
    g_driver = PM_WASMMOD_NET_CDN_DRIVER_PATH;
    g_token[0] = '\0';
    pm_wasmmod_io_set_auth_bearer(NULL);
    if (base_url != NULL && base_url[0] != '\0' && store_base_at(0, base_url)) {
        g_n_bases = 1;
        g_driver = PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS;
    }
    set_token(token);
}

int32_t pm_wasmmod_net_cdn_add(const char *base_url, const char *token) {
    if (base_url == NULL || base_url[0] == '\0' || already_have(base_url)) {
        return 0;
    }
    if (g_n_bases >= PM_WASMMOD_NET_CDN_BASES_MAX) {
        return 0;
    }
    if (!store_base_at(g_n_bases, base_url)) {
        return 0;
    }
    g_n_bases++;
    g_driver = PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS;
    if (token != NULL && token[0] != '\0') {
        set_token(token);
    }
    return 1;
}

int32_t pm_wasmmod_net_cdn_prepend(const char *base_url, const char *token) {
    if (base_url == NULL || base_url[0] == '\0' || already_have(base_url)) {
        return 0;
    }
    if (g_n_bases >= PM_WASMMOD_NET_CDN_BASES_MAX) {
        return 0;
    }
    for (unsigned i = g_n_bases; i > 0; --i) {
        memcpy(g_bases[i], g_bases[i - 1], PM_WASMMOD_NET_CDN_BASE_MAX);
    }
    if (!store_base_at(0, base_url)) {
        return 0;
    }
    g_n_bases++;
    g_driver = PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS;
    if (token != NULL && token[0] != '\0') {
        set_token(token);
    }
    return 1;
}

int32_t pm_wasmmod_net_cdn_driver(void) {
    return (int32_t)g_driver;
}

const char *pm_wasmmod_net_cdn_base(void) {
    return g_n_bases > 0 ? g_bases[0] : NULL;
}

uint32_t pm_wasmmod_net_cdn_base_count(void) {
    return g_n_bases;
}

const char *pm_wasmmod_net_cdn_base_at(uint32_t index) {
    return index < g_n_bases ? g_bases[index] : NULL;
}

int32_t pm_wasmmod_net_cdn_require_explicit_deps(void) {
    return g_driver == PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS ? 1 : 0;
}

const char *pm_wasmmod_net_cdn_driver_name(void) {
    return g_driver == PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS ? "artifacts" : "path";
}

void pm_wasmmod_net_cdn_set_session_id(const char *session_id) {
    g_session_id[0] = '\0';
    if (session_id == NULL || session_id[0] == '\0') {
        return;
    }
    size_t n = strlen(session_id);
    if (n >= PM_WASMMOD_NET_CDN_SESSION_ID_MAX) {
        n = PM_WASMMOD_NET_CDN_SESSION_ID_MAX - 1;
    }
    memcpy(g_session_id, session_id, n);
    g_session_id[n] = '\0';
}

const char *pm_wasmmod_net_cdn_session_id(void) {
    return g_session_id;
}

static int looks_like_artifact(const uint8_t *b, uint32_t n) {
    if (n >= 4 && b[0] == 'M' && b[1] == 'P' && b[2] == 'Z' && b[3] == 'L') {
        return 1; /* MPZL — load path unwraps */
    }
    if (n < 4) {
        return 0;
    }
    if (b[0] == 0x00 && b[1] == 'a' && b[2] == 's' && b[3] == 'm') {
        return 1;
    }
    if (b[0] == 0x00 && b[1] == 'a' && b[2] == 'o' && b[3] == 't') {
        return 1;
    }
    if (b[0] == 0x7f && b[1] == 'E' && b[2] == 'L' && b[3] == 'F') {
        return 1;
    }
    return 0;
}

static int32_t try_fetch_uri(const char *uri, uint8_t **out_bytes, uint32_t *out_len, char *errbuf,
    size_t errbuf_len) {
    uint8_t *buf = NULL;
    uint32_t n = 0;
    if (pm_wasmmod_io_fetch(uri, &buf, &n, errbuf, errbuf_len) != 0 || buf == NULL) {
        return -1;
    }
    if (!looks_like_artifact(buf, n)) {
        err_set(errbuf, errbuf_len, "cdn: not a wasm/aot/elf artifact");
        MICROPY_WASM_FREE(buf);
        return -1;
    }
    *out_bytes = buf;
    *out_len = n;
    return 0;
}

static unsigned collect_exts(const char **exts, unsigned cap, char *aot_ext, size_t aot_ext_len,
    char *aot_zlib, size_t aot_zlib_len) {
    unsigned n = 0;
    unsigned v = (unsigned)MICROPY_WASM_AOT_VERSION;
    if (v > 0) {
        snprintf(aot_ext, aot_ext_len, ".aot%u", v);
    } else {
        snprintf(aot_ext, aot_ext_len, ".aot");
    }
    snprintf(aot_zlib, aot_zlib_len, "%s.zlib", aot_ext);

    const char *pref = MICROPY_WASM_CONTAINERS;
    while (*pref && n + 2 < cap) {
        while (*pref == ',' || *pref == ' ') {
            pref++;
        }
        if (*pref == '\0') {
            break;
        }
        const char *start = pref;
        while (*pref && *pref != ',' && *pref != ' ') {
            pref++;
        }
        size_t kn = (size_t)(pref - start);
        if (kn == 3 && memcmp(start, "elf", 3) == 0) {
            exts[n++] = ".elf.zlib";
            exts[n++] = ".elf";
        } else if (kn == 3 && memcmp(start, "aot", 3) == 0) {
            exts[n++] = aot_zlib;
            exts[n++] = aot_ext;
            if (strcmp(aot_ext, ".aot") != 0 && n + 2 < cap) {
                exts[n++] = ".aot.zlib";
                exts[n++] = ".aot";
            }
        } else if (kn == 4 && memcmp(start, "wasm", 4) == 0) {
            exts[n++] = ".wasm.zlib";
            exts[n++] = ".wasm";
        }
    }
    if (n == 0 && cap >= 2) {
        exts[n++] = ".wasm.zlib";
        exts[n++] = ".wasm";
    }
    return n;
}

static int32_t artifact_fetch_one(const char *base, const char *name, const char *version,
    uint8_t **out_bytes, uint32_t *out_len, char *out_origin, uint32_t origin_len, char *errbuf,
    size_t errbuf_len) {
    const char *ver = (version != NULL) ? version : "";
    const char *exts[16];
    char aot_ext[16];
    char aot_zlib[24];
    unsigned n = collect_exts(exts, 16, aot_ext, sizeof(aot_ext), aot_zlib, sizeof(aot_zlib));
    char uri[PM_WASMMOD_NET_CDN_BASE_MAX + 160];

    if (ver[0] != '\0') {
        for (unsigned i = 0; i < n; ++i) {
            snprintf(uri, sizeof(uri), "%s/artifacts/pin/%s/%s%s", base, ver, name, exts[i]);
            if (try_fetch_uri(uri, out_bytes, out_len, errbuf, errbuf_len) == 0) {
                if (out_origin != NULL && origin_len > 0) {
                    snprintf(out_origin, origin_len, "%s", uri);
                }
                return 0;
            }
        }
    }
    for (unsigned i = 0; i < n; ++i) {
        snprintf(uri, sizeof(uri), "%s/artifacts/lead/%s%s", base, name, exts[i]);
        if (try_fetch_uri(uri, out_bytes, out_len, errbuf, errbuf_len) == 0) {
            if (out_origin != NULL && origin_len > 0) {
                snprintf(out_origin, origin_len, "%s", uri);
            }
            return 0;
        }
    }
    return -1;
}

int32_t pm_wasmmod_net_cdn_fetch_pack_ex(const char *name, const char *version, uint8_t **out_bytes,
    uint32_t *out_len, char *out_origin, uint32_t origin_len, char *errbuf, size_t errbuf_len) {
    if (out_bytes != NULL) {
        *out_bytes = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (out_origin != NULL && origin_len > 0) {
        out_origin[0] = '\0';
    }
    if (out_bytes == NULL || out_len == NULL) {
        err_set(errbuf, errbuf_len, "cdn: null out");
        return -1;
    }
    if (name == NULL || name[0] == '\0') {
        err_set(errbuf, errbuf_len, "cdn: empty name");
        return -1;
    }
    if (g_driver != PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS || g_n_bases == 0) {
        err_set(errbuf, errbuf_len, "cdn: configure an artifact base first");
        return -1;
    }
    for (unsigned bi = 0; bi < g_n_bases; ++bi) {
        if (artifact_fetch_one(g_bases[bi], name, version, out_bytes, out_len, out_origin, origin_len,
                errbuf, errbuf_len)
            == 0) {
            return 0;
        }
    }
    err_set(errbuf, errbuf_len, "cdn: package not found");
    return -1;
}

int32_t pm_wasmmod_net_cdn_fetch_pack(const char *name, const char *version, uint8_t **out_bytes,
    uint32_t *out_len, char *errbuf, size_t errbuf_len) {
    return pm_wasmmod_net_cdn_fetch_pack_ex(name, version, out_bytes, out_len, NULL, 0, errbuf,
        errbuf_len);
}

int32_t pm_wasmmod_net_cdn_fetch_index(const char *channel, uint8_t **out_bytes, uint32_t *out_len,
    char *errbuf, size_t errbuf_len) {
    if (out_bytes != NULL) {
        *out_bytes = NULL;
    }
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (out_bytes == NULL || out_len == NULL) {
        err_set(errbuf, errbuf_len, "cdn: null out");
        return -1;
    }
    if (g_n_bases == 0 || g_driver != PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS) {
        err_set(errbuf, errbuf_len, "cdn: configure an artifact base first");
        return -1;
    }
    const char *ch = (channel != NULL && channel[0] != '\0') ? channel : "lead";
    char uri[PM_WASMMOD_NET_CDN_BASE_MAX + 96];
    for (unsigned bi = 0; bi < g_n_bases; ++bi) {
        const char *base = g_bases[bi];
        if (ch[0] == '@') {
            snprintf(uri, sizeof(uri), "%s/index/pin/%s", base, ch + 1);
        } else if (strncmp(ch, "pin/", 4) == 0) {
            snprintf(uri, sizeof(uri), "%s/index/pin/%s", base, ch + 4);
        } else {
            snprintf(uri, sizeof(uri), "%s/index/%s", base, ch);
        }
        uint8_t *buf = NULL;
        uint32_t n = 0;
        if (pm_wasmmod_io_fetch(uri, &buf, &n, errbuf, errbuf_len) != 0 || buf == NULL) {
            continue;
        }
        *out_bytes = buf;
        *out_len = n;
        return 0;
    }
    err_set(errbuf, errbuf_len, "cdn: index not found");
    return -1;
}

int32_t pm_wasmmod_net_cdn_publish(const char *name, const char *version, const uint8_t *data,
    uint32_t data_len, int32_t lead, int32_t pin, const char *token, char *errbuf,
    size_t errbuf_len) {
    if (name == NULL || name[0] == '\0') {
        err_set(errbuf, errbuf_len, "cdn: empty name");
        return -1;
    }
    if (!lead && !pin) {
        err_set(errbuf, errbuf_len, "cdn: lead or pin required");
        return -1;
    }
    if (pin && (version == NULL || version[0] == '\0')) {
        err_set(errbuf, errbuf_len, "cdn: pin requires version");
        return -1;
    }
    if (data == NULL && data_len > 0) {
        err_set(errbuf, errbuf_len, "cdn: null data");
        return -1;
    }
    if (g_driver != PM_WASMMOD_NET_CDN_DRIVER_ARTIFACTS || g_n_bases == 0) {
        err_set(errbuf, errbuf_len, "cdn: configure an artifact base first");
        return -1;
    }

    char saved[PM_WASMMOD_NET_CDN_TOKEN_MAX];
    const char *cur = pm_wasmmod_io_auth_bearer();
    snprintf(saved, sizeof(saved), "%s", cur != NULL ? cur : "");
    int restore = 0;
    if (token != NULL && token[0] != '\0') {
        pm_wasmmod_io_set_auth_bearer(token);
        restore = 1;
    }

    /* Outer magic only — CDN does not inflate MPZL (finder/load unwraps). */
    int wrapped = (data != NULL && data_len >= 4 && data[0] == 'M' && data[1] == 'P' && data[2] == 'Z'
        && data[3] == 'L');
    const char *kind = ".wasm";
    if (!wrapped && data != NULL && data_len >= 4) {
        if (data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
            kind = ".elf";
        } else if (data[0] == 0x00 && data[1] == 'a' && data[2] == 'o' && data[3] == 't') {
            kind = ".aot";
        }
    }
    char ext[24];
    if (wrapped) {
        snprintf(ext, sizeof(ext), "%s.zlib", kind);
    } else {
        snprintf(ext, sizeof(ext), "%s", kind);
    }

    char uri[PM_WASMMOD_NET_CDN_BASE_MAX + 160];
    int32_t st = 0;
    if (lead) {
        st = -1;
        for (unsigned bi = 0; bi < g_n_bases; ++bi) {
            snprintf(uri, sizeof(uri), "%s/artifacts/lead/%s%s", g_bases[bi], name, ext);
            if (pm_wasmmod_io_request("POST", uri, data, data_len, "application/octet-stream", NULL,
                    NULL, errbuf, errbuf_len)
                == 0) {
                st = 0;
                break;
            }
        }
        if (st != 0) {
            if (restore) {
                pm_wasmmod_io_set_auth_bearer(saved[0] ? saved : NULL);
            }
            return -1;
        }
    }
    if (pin) {
        st = -1;
        for (unsigned bi = 0; bi < g_n_bases; ++bi) {
            snprintf(uri, sizeof(uri), "%s/artifacts/pin/%s/%s%s", g_bases[bi], version, name, ext);
            if (pm_wasmmod_io_request("POST", uri, data, data_len, "application/octet-stream", NULL,
                    NULL, errbuf, errbuf_len)
                == 0) {
                st = 0;
                break;
            }
        }
        if (st != 0) {
            if (restore) {
                pm_wasmmod_io_set_auth_bearer(saved[0] ? saved : NULL);
            }
            return -1;
        }
    }
    if (restore) {
        pm_wasmmod_io_set_auth_bearer(saved[0] ? saved : NULL);
    }
    return 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_configure, pm_wasmmod_net_cdn_configure, void(const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_reset, pm_wasmmod_net_cdn_reset, void(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_add, pm_wasmmod_net_cdn_add, int32_t(const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_prepend, pm_wasmmod_net_cdn_prepend, int32_t(const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_driver, pm_wasmmod_net_cdn_driver, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_base, pm_wasmmod_net_cdn_base, const char *(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_base_count, pm_wasmmod_net_cdn_base_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_base_at, pm_wasmmod_net_cdn_base_at, const char *(uint32_t));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_require_explicit_deps, pm_wasmmod_net_cdn_require_explicit_deps, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_fetch_pack, pm_wasmmod_net_cdn_fetch_pack, int32_t(const char *, const char *, uint8_t **, uint32_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_fetch_pack_ex, pm_wasmmod_net_cdn_fetch_pack_ex, int32_t(const char *, const char *, uint8_t **, uint32_t *, char *, uint32_t, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_fetch_index, pm_wasmmod_net_cdn_fetch_index, int32_t(const char *, uint8_t **, uint32_t *, char *, size_t));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_url_is_base, pm_wasmmod_net_cdn_url_is_base, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_driver_name, pm_wasmmod_net_cdn_driver_name, const char *(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_set_session_id, pm_wasmmod_net_cdn_set_session_id, void(const char *));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_session_id, pm_wasmmod_net_cdn_session_id, const char *(void));
PM_MOD_EXPORT_C(pymergetic.wasmmod.net.cdn, pm_wasmmod_net_cdn_publish, pm_wasmmod_net_cdn_publish, int32_t(const char *, const char *, const uint8_t *, uint32_t, int32_t, int32_t, const char *, char *, size_t));
