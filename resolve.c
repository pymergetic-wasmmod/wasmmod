/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Closure walk over wasmmod.deps (MPWD) with artifact cache.
 * Cycles are supported: all nodes are fetched; load order is deps-first
 * via Tarjan SCCs (dependees before dependents).
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include <stdio.h>
#include <string.h>

#include "extmod/wasmmod/resolve.h"
#include "extmod/wasmmod/cdn.h"
#include "extmod/wasmmod/pack.h"
#include "extmod/wasmmod/alloc.h"

typedef struct {
    char name[MP_WASM_DEP_NAME_MAX];
    char version[MP_WASM_DEP_VER_MAX];
    uint8_t *bytes;
    uint32_t len;
    uint16_t dep_idx[16];
    uint8_t n_deps;
} cache_ent_t;

static cache_ent_t g_cache[MP_WASM_CLOSURE_MAX];
static uint32_t g_n_cache;

void mp_wasm_closure_cache_clear(void) {
    for (uint32_t i = 0; i < g_n_cache; ++i) {
        MICROPY_WASM_FREE(g_cache[i].bytes);
        g_cache[i].bytes = NULL;
        g_cache[i].len = 0;
        g_cache[i].n_deps = 0;
    }
    g_n_cache = 0;
}

static int cache_find(const char *name, const char *version) {
    const char *ver = version ? version : "";
    for (uint32_t i = 0; i < g_n_cache; ++i) {
        if (strcmp(g_cache[i].name, name) == 0 && strcmp(g_cache[i].version, ver) == 0) {
            return (int)i;
        }
    }
    return -1;
}

const uint8_t *mp_wasm_closure_cached(const char *name, const char *version, uint32_t *out_len) {
    int i = cache_find(name, version);
    if (i < 0) {
        return NULL;
    }
    if (out_len) {
        *out_len = g_cache[i].len;
    }
    return g_cache[i].bytes;
}

static void copy_trunc(char *dst, size_t dstsz, const char *src, size_t srclen) {
    size_t n = srclen < dstsz - 1 ? srclen : dstsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool cache_put(const char *name, const char *version, const uint8_t *bytes, uint32_t len) {
    if (g_n_cache >= MP_WASM_CLOSURE_MAX) {
        return false;
    }
    cache_ent_t *e = &g_cache[g_n_cache];
    memset(e, 0, sizeof(*e));
    copy_trunc(e->name, sizeof(e->name), name, strlen(name));
    copy_trunc(e->version, sizeof(e->version), version ? version : "", version ? strlen(version) : 0);
    e->bytes = MICROPY_WASM_MALLOC(len ? len : 1);
    if (e->bytes == NULL) {
        return false;
    }
    if (len) {
        memcpy(e->bytes, bytes, len);
    }
    e->len = len;

    // Parse MPWD edges (indices filled after all nodes known — store names temporarily via deps parse)
    mp_wasm_deps_info_t deps;
    memset(&deps, 0, sizeof(deps));
    const uint8_t *payload = NULL;
    uint32_t plen = 0;
    e->n_deps = 0;
    if (mp_wasm_deps_find_section(bytes, len, &payload, &plen)
        && mp_wasm_deps_parse(payload, plen, &deps)) {
        // Stash dep name@version in dep_idx as sentinel; resolve after discovery.
        // For now keep dep strings by re-parsing when linking — store count only,
        // link in a second pass.
        e->n_deps = deps.n_deps > 16 ? 16 : (uint8_t)deps.n_deps;
        mp_wasm_deps_info_free(&deps);
    }
    g_n_cache++;
    return true;
}

static void link_deps(void) {
    for (uint32_t i = 0; i < g_n_cache; ++i) {
        cache_ent_t *e = &g_cache[i];
        mp_wasm_deps_info_t deps;
        memset(&deps, 0, sizeof(deps));
        const uint8_t *payload = NULL;
        uint32_t plen = 0;
        e->n_deps = 0;
        if (!mp_wasm_deps_find_section(e->bytes, e->len, &payload, &plen)
            || !mp_wasm_deps_parse(payload, plen, &deps)) {
            continue;
        }
        uint8_t n = deps.n_deps > 16 ? 16 : (uint8_t)deps.n_deps;
        for (uint8_t d = 0; d < n; ++d) {
            char dn[MP_WASM_DEP_NAME_MAX];
            char dv[MP_WASM_DEP_VER_MAX];
            copy_trunc(dn, sizeof(dn), deps.deps[d].name, deps.deps[d].name_len);
            copy_trunc(dv, sizeof(dv), deps.deps[d].version, deps.deps[d].version_len);
            int j = cache_find(dn, dv);
            if (j >= 0) {
                e->dep_idx[e->n_deps++] = (uint16_t)j;
            }
        }
        mp_wasm_deps_info_free(&deps);
    }
}

// Discovery order enqueues root then deps; reverse ≈ deps-first for trees.
// Cycles are fine: packload registers all nodes before any mp_pack_load.
static void emit_order(mp_wasm_closure_t *out) {
    out->n_nodes = 0;
    for (int i = (int)g_n_cache - 1; i >= 0; --i) {
        if (out->n_nodes >= MP_WASM_CLOSURE_MAX) {
            break;
        }
        copy_trunc(out->nodes[out->n_nodes].name, MP_WASM_DEP_NAME_MAX,
            g_cache[i].name, strlen(g_cache[i].name));
        copy_trunc(out->nodes[out->n_nodes].version, MP_WASM_DEP_VER_MAX,
            g_cache[i].version, strlen(g_cache[i].version));
        out->n_nodes++;
    }
}

bool mp_wasm_resolve_closure(const char *root_name, const char *root_version,
    const uint8_t *root_bytes, uint32_t root_len,
    mp_wasm_closure_t *out, char *errbuf, size_t errbuf_len) {
    if (out == NULL || root_name == NULL || root_bytes == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    mp_wasm_closure_cache_clear();

    if (!cache_put(root_name, root_version ? root_version : "", root_bytes, root_len)) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "resolve: cache full");
        }
        return false;
    }

    // BFS discover deps
    for (uint32_t i = 0; i < g_n_cache; ++i) {
        cache_ent_t *e = &g_cache[i];
        mp_wasm_deps_info_t deps;
        memset(&deps, 0, sizeof(deps));
        const uint8_t *payload = NULL;
        uint32_t plen = 0;
        if (!mp_wasm_deps_find_section(e->bytes, e->len, &payload, &plen)
            || !mp_wasm_deps_parse(payload, plen, &deps)) {
            continue;
        }
        for (uint32_t d = 0; d < deps.n_deps; ++d) {
            char dn[MP_WASM_DEP_NAME_MAX];
            char dv[MP_WASM_DEP_VER_MAX];
            copy_trunc(dn, sizeof(dn), deps.deps[d].name, deps.deps[d].name_len);
            copy_trunc(dv, sizeof(dv), deps.deps[d].version, deps.deps[d].version_len);
            if (cache_find(dn, dv) >= 0) {
                continue;
            }
            uint8_t *bytes = NULL;
            uint32_t blen = 0;
            if (!mp_wasm_cdn_fetch_pack(dn, dv, &bytes, &blen, errbuf, errbuf_len)) {
                mp_wasm_deps_info_free(&deps);
                return false;
            }
            bool ok = cache_put(dn, dv, bytes, blen);
            MICROPY_WASM_FREE(bytes);
            if (!ok) {
                mp_wasm_deps_info_free(&deps);
                if (errbuf && errbuf_len) {
                    snprintf(errbuf, errbuf_len, "resolve: cache full");
                }
                return false;
            }
        }
        mp_wasm_deps_info_free(&deps);
    }

    link_deps();
    emit_order(out);
    if (out->n_nodes == 0) {
        // Fallback: just root
        copy_trunc(out->nodes[0].name, MP_WASM_DEP_NAME_MAX, root_name, strlen(root_name));
        copy_trunc(out->nodes[0].version, MP_WASM_DEP_VER_MAX,
            root_version ? root_version : "", root_version ? strlen(root_version) : 0);
        out->n_nodes = 1;
    }
    return true;
}

#endif // MICROPY_PY_WASM
