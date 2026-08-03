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

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extmod/wasmmod/pack.h"
#include "extmod/wasmmod/verify.h"

#ifndef MICROPY_WASM_MALLOC
#define MICROPY_WASM_MALLOC(n) malloc(n)
#endif
#ifndef MICROPY_WASM_FREE
#define MICROPY_WASM_FREE(p) free(p)
#endif

typedef struct mp_wasm_trust_key_t {
    struct mp_wasm_trust_key_t *next;
    uint8_t *key;
    size_t key_len;
} mp_wasm_trust_key_t;

static mp_wasm_trust_key_t *trust_keys;
static size_t trust_n;

// Default on; wasm.verify(False) disables for the session (all loads).
static bool verify_runtime_enabled = true;

void mp_wasm_set_verify_enabled(bool enabled) {
    verify_runtime_enabled = enabled;
}

bool mp_wasm_get_verify_enabled(void) {
    return verify_runtime_enabled;
}

bool mp_wasm_trust_add(const uint8_t *key, size_t key_len) {
    if (key == NULL || key_len == 0) {
        return false;
    }
    mp_wasm_trust_key_t *node = MICROPY_WASM_MALLOC(sizeof(*node));
    if (node == NULL) {
        return false;
    }
    node->key = MICROPY_WASM_MALLOC(key_len);
    if (node->key == NULL) {
        MICROPY_WASM_FREE(node);
        return false;
    }
    memcpy(node->key, key, key_len);
    node->key_len = key_len;
    node->next = trust_keys;
    trust_keys = node;
    trust_n++;
    return true;
}

void mp_wasm_trust_clear(void) {
    while (trust_keys != NULL) {
        mp_wasm_trust_key_t *dead = trust_keys;
        trust_keys = dead->next;
        MICROPY_WASM_FREE(dead->key);
        MICROPY_WASM_FREE(dead);
    }
    trust_n = 0;
}

size_t mp_wasm_trust_count(void) {
    return trust_n;
}

#if MICROPY_WASM_VERIFY

#include "extmod/wasmmod/fetch.h"

#if MICROPY_SSL_MBEDTLS
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
#endif

// Embedded section envelope: MPWS + ver + flags + sig + chain (leaf first).
#define MP_WASM_MPWS_MAGIC "MPWS"
#define MP_WASM_MPWS_VER 1

static bool load_sidecar(const char *path_hint, const char *suffix, vstr_t *out) {
    if (path_hint == NULL) {
        return false;
    }
    vstr_t path;
    vstr_init(&path, strlen(path_hint) + strlen(suffix) + 1);
    vstr_add_str(&path, path_hint);
    vstr_add_str(&path, suffix);
    char err[64];
    bool ok = mp_wasm_fetch(vstr_null_terminated_str(&path), out, err, sizeof(err));
    vstr_clear(&path);
    return ok;
}

static bool load_section_sig(const uint8_t *bytes, uint32_t len, const uint8_t **sig, uint32_t *sig_len) {
    return mp_wasm_find_custom_section(bytes, len, MP_WASM_SIG_SECTION, sig, sig_len);
}

// Copy wasm omitting wasmmod.sig custom sections (signed payload for --embed).
static bool copy_without_sig_section(const uint8_t *wasm, uint32_t len, uint8_t **out, uint32_t *out_len) {
    if (wasm == NULL || len < 8
        || wasm[0] != 0x00 || wasm[1] != 'a' || wasm[2] != 's' || wasm[3] != 'm') {
        return false;
    }
    uint8_t *buf = MICROPY_WASM_MALLOC(len);
    if (buf == NULL) {
        return false;
    }
    memcpy(buf, wasm, 8);
    uint32_t w = 8;
    const uint8_t *p = wasm + 8;
    const uint8_t *end = wasm + len;
    const size_t want_len = strlen(MP_WASM_SIG_SECTION);

    while (p < end) {
        const uint8_t *sec_start = p;
        uint8_t id = *p++;
        uint32_t size;
        if (!mp_wasm_read_uleb(&p, end, &size) || p + size > end) {
            MICROPY_WASM_FREE(buf);
            return false;
        }
        const uint8_t *payload = p;
        p += size;
        bool skip = false;
        if (id == 0) {
            const uint8_t *q = payload;
            uint32_t name_len;
            if (mp_wasm_read_uleb(&q, payload + size, &name_len)
                && q + name_len <= payload + size
                && name_len == want_len
                && memcmp(q, MP_WASM_SIG_SECTION, want_len) == 0) {
                skip = true;
            }
        }
        if (!skip) {
            size_t n = (size_t)(p - sec_start);
            memcpy(buf + w, sec_start, n);
            w += (uint32_t)n;
        }
    }
    *out = buf;
    *out_len = w;
    return true;
}

// Split MPWS envelope or treat payload as raw ECDSA sig.
static bool parse_sig_payload(const uint8_t *payload, uint32_t payload_len,
    const uint8_t **sig, uint32_t *sig_len,
    const uint8_t **chain, uint32_t *chain_len) {
    *chain = NULL;
    *chain_len = 0;
    if (payload_len >= 8
        && memcmp(payload, MP_WASM_MPWS_MAGIC, 4) == 0
        && payload[4] == MP_WASM_MPWS_VER) {
        uint32_t sl = ((uint32_t)payload[6] << 8) | payload[7];
        if (8u + sl > payload_len) {
            return false;
        }
        *sig = payload + 8;
        *sig_len = sl;
        uint32_t rest = payload_len - 8u - sl;
        if (rest >= 2) {
            const uint8_t *cp = payload + 8 + sl;
            uint32_t cl = ((uint32_t)cp[0] << 8) | cp[1];
            if (2u + cl > rest) {
                return false;
            }
            *chain = cp + 2;
            *chain_len = cl;
        }
        return *sig_len > 0;
    }
    *sig = payload;
    *sig_len = payload_len;
    return payload_len > 0;
}

#if MICROPY_SSL_MBEDTLS

static bool der_one_cert_len(const uint8_t *p, size_t left, size_t *out_len) {
    if (left < 2 || p[0] != 0x30) {
        return false;
    }
    size_t hdr;
    size_t len;
    if ((p[1] & 0x80) == 0) {
        len = p[1];
        hdr = 2;
    } else {
        size_t n = (size_t)(p[1] & 0x7f);
        if (n == 0 || n > 3 || left < 2 + n) {
            return false;
        }
        len = 0;
        for (size_t i = 0; i < n; i++) {
            len = (len << 8) | p[2 + i];
        }
        hdr = 2 + n;
    }
    if (hdr + len < hdr || hdr + len > left) {
        return false;
    }
    *out_len = hdr + len;
    return true;
}

static bool parse_der_cert_chain(mbedtls_x509_crt *chain, const uint8_t *buf, size_t len) {
    const uint8_t *p = buf;
    size_t left = len;
    while (left > 0) {
        size_t one;
        if (!der_one_cert_len(p, left, &one)) {
            return false;
        }
        if (mbedtls_x509_crt_parse_der(chain, p, one) != 0) {
            return false;
        }
        p += one;
        left -= one;
    }
    return chain->raw.p != NULL;
}

static bool verify_ecdsa_sha256_pk(const uint8_t *bytes, uint32_t len, const uint8_t *sig, uint32_t sig_len, mbedtls_pk_context *pk) {
    unsigned char hash[32];
    if (mbedtls_sha256(bytes, len, hash, 0) != 0) {
        return false;
    }
    if (!mbedtls_pk_can_do(pk, MBEDTLS_PK_ECDSA)) {
        return false;
    }
    return mbedtls_pk_verify(pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sig_len) == 0;
}

static bool verify_ecdsa_sha256_spki(const uint8_t *bytes, uint32_t len, const uint8_t *sig, uint32_t sig_len, const uint8_t *key, size_t key_len) {
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_parse_public_key(&pk, key, key_len);
    bool ok = false;
    if (ret == 0) {
        ok = verify_ecdsa_sha256_pk(bytes, len, sig, sig_len, &pk);
    }
    mbedtls_pk_free(&pk);
    return ok;
}

// Trust anchors may be CA cert DER/PEM; build mbedtls trust list.
static bool load_trust_cas(mbedtls_x509_crt *trust) {
    bool any = false;
    for (mp_wasm_trust_key_t *k = trust_keys; k != NULL; k = k->next) {
        // DER (single or we only take first) then PEM (null-terminated copy).
        if (mbedtls_x509_crt_parse_der(trust, k->key, k->key_len) == 0) {
            any = true;
            continue;
        }
        uint8_t *pem = MICROPY_WASM_MALLOC(k->key_len + 1);
        if (pem == NULL) {
            continue;
        }
        memcpy(pem, k->key, k->key_len);
        pem[k->key_len] = 0;
        if (mbedtls_x509_crt_parse(trust, pem, k->key_len + 1) == 0) {
            any = true;
        }
        MICROPY_WASM_FREE(pem);
    }
    return any;
}

static bool verify_pki_chain(const uint8_t *bytes, uint32_t len, const uint8_t *sig, uint32_t sig_len,
    const uint8_t *chain_der, uint32_t chain_len) {
    if (chain_der == NULL || chain_len == 0 || trust_n == 0) {
        return false;
    }

    mbedtls_x509_crt leaf;
    mbedtls_x509_crt trust;
    mbedtls_x509_crt_init(&leaf);
    mbedtls_x509_crt_init(&trust);

    bool ok = false;
    if (!parse_der_cert_chain(&leaf, chain_der, chain_len)) {
        goto done;
    }
    if (!load_trust_cas(&trust)) {
        goto done;
    }

    uint32_t flags = 0;
    if (mbedtls_x509_crt_verify(&leaf, &trust, NULL, NULL, &flags, NULL, NULL) != 0) {
        goto done;
    }

    ok = verify_ecdsa_sha256_pk(bytes, len, sig, sig_len, &leaf.pk);

done:
    mbedtls_x509_crt_free(&leaf);
    mbedtls_x509_crt_free(&trust);
    return ok;
}

#endif // MICROPY_SSL_MBEDTLS

static bool verify_with_trust(const uint8_t *bytes, uint32_t len, const uint8_t *sig, uint32_t sig_len,
    const uint8_t *chain, uint32_t chain_len) {
    #ifdef MICROPY_WASM_VERIFY_HOOK
    (void)chain;
    (void)chain_len;
    return MICROPY_WASM_VERIFY_HOOK(bytes, len, sig, sig_len);
    #elif MICROPY_SSL_MBEDTLS
    if (trust_n == 0) {
        return false;
    }
    if (chain != NULL && chain_len > 0) {
        if (verify_pki_chain(bytes, len, sig, sig_len, chain, chain_len)) {
            return true;
        }
        // Fall through: allow pinned leaf SPKI even if .crt present but CA trust missing.
    }
    for (mp_wasm_trust_key_t *k = trust_keys; k != NULL; k = k->next) {
        if (verify_ecdsa_sha256_spki(bytes, len, sig, sig_len, k->key, k->key_len)) {
            return true;
        }
    }
    return false;
    #else
    (void)bytes;
    (void)len;
    (void)sig;
    (void)sig_len;
    (void)chain;
    (void)chain_len;
    return false;
    #endif
}

bool mp_wasm_verify_bytes(const uint8_t *bytes, uint32_t len, const char *path_hint, char *errbuf, size_t errbuf_len) {
    if (!verify_runtime_enabled) {
        return true;
    }
    if (bytes == NULL || len == 0) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "verify: empty");
        }
        return false;
    }

    vstr_t detached;
    bool have_detached = load_sidecar(path_hint, ".sig", &detached);
    vstr_t crt;
    bool have_crt = load_sidecar(path_hint, ".crt", &crt);
    const uint8_t *sec_payload = NULL;
    uint32_t sec_payload_len = 0;
    bool have_section = load_section_sig(bytes, len, &sec_payload, &sec_payload_len);

    const uint8_t *sig = NULL;
    uint32_t sig_len = 0;
    const uint8_t *chain = NULL;
    uint32_t chain_len = 0;
    const uint8_t *hash_bytes = bytes;
    uint32_t hash_len = len;
    uint8_t *stripped = NULL;
    uint32_t stripped_len = 0;

    // Prefer embedded section (signed over module without wasmmod.sig).
    // Else detached .sig (+ optional .crt) over the exact artifact bytes.
    if (have_section) {
        if (!copy_without_sig_section(bytes, len, &stripped, &stripped_len)) {
            if (have_detached) {
                vstr_clear(&detached);
            }
            if (have_crt) {
                vstr_clear(&crt);
            }
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "verify: bad wasmmod.sig layout");
            }
            return false;
        }
        hash_bytes = stripped;
        hash_len = stripped_len;
        if (!parse_sig_payload(sec_payload, sec_payload_len, &sig, &sig_len, &chain, &chain_len)) {
            MICROPY_WASM_FREE(stripped);
            if (have_detached) {
                vstr_clear(&detached);
            }
            if (have_crt) {
                vstr_clear(&crt);
            }
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "verify: bad wasmmod.sig payload");
            }
            return false;
        }
        // Detached .crt may supplement a raw (non-MPWS) embedded sig.
        if (chain_len == 0 && have_crt) {
            chain = (const uint8_t *)crt.buf;
            chain_len = (uint32_t)crt.len;
        }
    } else if (have_detached) {
        if (!parse_sig_payload((const uint8_t *)detached.buf, (uint32_t)detached.len, &sig, &sig_len, &chain, &chain_len)) {
            vstr_clear(&detached);
            if (have_crt) {
                vstr_clear(&crt);
            }
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "verify: bad signature");
            }
            return false;
        }
        if (chain_len == 0 && have_crt) {
            chain = (const uint8_t *)crt.buf;
            chain_len = (uint32_t)crt.len;
        }
    } else {
        if (have_crt) {
            vstr_clear(&crt);
        }
        #if MICROPY_WASM_VERIFY == 1
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "verify: signature required");
        }
        return false;
        #else
        return true;
        #endif
    }

    bool ok = verify_with_trust(hash_bytes, hash_len, sig, sig_len, chain, chain_len);
    if (stripped != NULL) {
        MICROPY_WASM_FREE(stripped);
    }
    if (have_detached) {
        vstr_clear(&detached);
    }
    if (have_crt) {
        vstr_clear(&crt);
    }
    if (!ok) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "verify: bad signature");
        }
        return false;
    }
    return true;
}

#else // !MICROPY_WASM_VERIFY

bool mp_wasm_verify_bytes(const uint8_t *bytes, uint32_t len, const char *path_hint, char *errbuf, size_t errbuf_len) {
    (void)bytes;
    (void)len;
    (void)path_hint;
    (void)errbuf;
    (void)errbuf_len;
    (void)verify_runtime_enabled; // still settable; compile-time off = always allow
    return true;
}

#endif // MICROPY_WASM_VERIFY

#endif // MICROPY_PY_WASM
