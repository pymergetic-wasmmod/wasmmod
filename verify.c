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
#include <string.h>

#include "py/mpconfig.h"
#include "extmod/wasmmod/verify.h"

#include "extmod/wasmmod/alloc.h"
#ifndef MP_WEAK
#define MP_WEAK __attribute__((weak))
#endif

typedef struct mp_wasm_trust_key_t {
    struct mp_wasm_trust_key_t *next;
    uint8_t *key;
    size_t key_len;
} mp_wasm_trust_key_t;

static mp_wasm_trust_key_t *trust_keys;
static size_t trust_n;

// Lazy baked-CA load: armed on session init; disarmed by trust_clear().
static bool trust_builtin_armed;
static bool trust_builtin_loaded;

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

static void trust_clear_list(void) {
    while (trust_keys != NULL) {
        mp_wasm_trust_key_t *dead = trust_keys;
        trust_keys = dead->next;
        MICROPY_WASM_FREE(dead->key);
        MICROPY_WASM_FREE(dead);
    }
    trust_n = 0;
}

void mp_wasm_trust_clear(void) {
    trust_clear_list();
    trust_builtin_armed = false;
    trust_builtin_loaded = false;
}

void mp_wasm_trust_init_session(void) {
    trust_clear_list();
    trust_builtin_armed = true;
    trust_builtin_loaded = false;
}

// Overridden by BUILD/wasm_trust_ca.c when MICROPY_WASM_TRUST_CA is set.
MP_WEAK void mp_wasm_trust_load_builtin(void) {
}

void mp_wasm_trust_ensure(void) {
    if (trust_builtin_loaded || !trust_builtin_armed) {
        return;
    }
    trust_builtin_loaded = true;
    mp_wasm_trust_load_builtin();
    #ifdef MICROPY_WASM_TRUST_BOOT
    MICROPY_WASM_TRUST_BOOT();
    #endif
}

size_t mp_wasm_trust_count(void) {
    mp_wasm_trust_ensure();
    return trust_n;
}

#ifndef MICROPY_WASM_TRUST_INFLATE
#define MICROPY_WASM_TRUST_INFLATE (0)
#endif

#if MICROPY_WASM_TRUST_INFLATE || MICROPY_PY_DEFLATE
#include "extmod/wasmmod/zlibutil.h"
#endif

bool mp_wasm_trust_add_blob(const uint8_t *data, uint32_t data_len, uint32_t uncompressed_len) {
    if (data == NULL || data_len == 0 || uncompressed_len == 0) {
        return false;
    }
    if (data_len == uncompressed_len) {
        return mp_wasm_trust_add(data, uncompressed_len);
    }
    #if MICROPY_WASM_TRUST_INFLATE || MICROPY_PY_DEFLATE
    uint8_t *raw = MICROPY_WASM_MALLOC(uncompressed_len);
    if (raw == NULL) {
        return false;
    }
    bool ok = mp_wasm_zlib_inflate(data, data_len, raw, uncompressed_len);
    if (ok) {
        ok = mp_wasm_trust_add(raw, uncompressed_len);
    }
    MICROPY_WASM_FREE(raw);
    return ok;
    #else
    (void)data;
    return false;
    #endif
}

#if MICROPY_WASM_VERIFY

#include "extmod/wasmmod/pack.h"

#if MICROPY_SSL_MBEDTLS
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
#endif

// Embedded section envelope: MPWS + ver + flags + sig + chain (leaf first).
#define MP_WASM_MPWS_MAGIC "MPWS"
#define MP_WASM_MPWS_VER 1

bool mp_wasm_sig_find(const uint8_t *bytes, uint32_t len, const uint8_t **payload, uint32_t *payload_len) {
    return mp_wasm_find_custom_section(bytes, len, MP_WASM_SIG_SECTION, payload, payload_len);
}

// Read little-endian helpers (AOT section walk; pack.c has its own static copies).
static uint16_t verify_read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t verify_read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool mp_wasm_sig_strip(const uint8_t *mod, uint32_t len, uint8_t **out, uint32_t *out_len) {
    if (mod == NULL || len < 8 || mod[0] != 0x00) {
        return false;
    }
    const bool is_wasm = (mod[1] == 'a' && mod[2] == 's' && mod[3] == 'm');
    const bool is_aot = (mod[1] == 'a' && mod[2] == 'o' && mod[3] == 't');
    if (!is_wasm && !is_aot) {
        return false;
    }

    uint8_t *buf = MICROPY_WASM_MALLOC(len);
    if (buf == NULL) {
        return false;
    }
    memcpy(buf, mod, 8);
    uint32_t w = 8;
    const size_t want_len = strlen(MP_WASM_SIG_SECTION);

    if (is_wasm) {
        const uint8_t *p = mod + 8;
        const uint8_t *end = mod + len;
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
    } else {
        // AOT: type/size sections; CUSTOM(100)/RAW(0) with EMIT_STR name.
        uintptr_t p = 8;
        while (p + 8 <= len) {
            const uint8_t *sec_start = mod + p;
            uint32_t typ = verify_read_u32_le(mod + p);
            uint32_t size = verify_read_u32_le(mod + p + 4);
            const uint8_t *content = mod + p + 8;
            if (content + size > mod + len || size > 0x10000000u) {
                MICROPY_WASM_FREE(buf);
                return false;
            }
            // Next header is 4-aligned (WAMR read_uint32 align_ptr).
            uintptr_t aligned = ((uintptr_t)(content + size - mod) + 3u) & ~(uintptr_t)3u;
            uintptr_t next = aligned <= len ? aligned : len;
            bool skip = false;
            if (typ == 100 && size >= 6) {
                uint32_t sub = verify_read_u32_le(content);
                if (sub == 0) {
                    uint16_t slen = verify_read_u16_le(content + 4);
                    const uint8_t *nb = content + 6;
                    if (nb + slen <= content + size) {
                        size_t bare = slen;
                        if (bare > 0 && nb[bare - 1] == 0) {
                            bare--;
                        }
                        if (bare == want_len && memcmp(nb, MP_WASM_SIG_SECTION, want_len) == 0) {
                            skip = true;
                        }
                    }
                }
            }
            if (!skip) {
                size_t n = (size_t)(next - p);
                memcpy(buf + w, sec_start, n);
                w += (uint32_t)n;
            } else {
                // Drop trailing pad after wasmmod.sig (not part of signed body).
                break;
            }
            p = next;
        }
    }

    *out = buf;
    *out_len = w;
    return true;
}

// Split MPWS envelope or treat payload as raw ECDSA sig.
bool mp_wasm_sig_parse(const uint8_t *payload, uint32_t payload_len, mp_wasm_sig_info_t *out) {
    if (out == NULL || payload == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (payload_len >= 8
        && memcmp(payload, MP_WASM_MPWS_MAGIC, 4) == 0
        && payload[4] == MP_WASM_MPWS_VER) {
        uint32_t sl = ((uint32_t)payload[6] << 8) | payload[7];
        if (8u + sl > payload_len || sl == 0) {
            return false;
        }
        out->sig = payload + 8;
        out->sig_len = sl;
        out->is_mpws = true;
        uint32_t rest = payload_len - 8u - sl;
        if (rest >= 2) {
            const uint8_t *cp = payload + 8 + sl;
            uint32_t cl = ((uint32_t)cp[0] << 8) | cp[1];
            if (2u + cl > rest) {
                return false;
            }
            out->chain = cp + 2;
            out->chain_len = cl;
        }
        return true;
    }
    if (payload_len == 0) {
        return false;
    }
    out->sig = payload;
    out->sig_len = payload_len;
    out->is_mpws = false;
    return true;
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
        // Fall through: allow pinned leaf SPKI even if chain fails CA trust.
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
    (void)path_hint;
    if (!verify_runtime_enabled) {
        return true;
    }
    return mp_wasm_verify_sig(bytes, len, errbuf, errbuf_len);
}

bool mp_wasm_verify_sig(const uint8_t *bytes, uint32_t len, char *errbuf, size_t errbuf_len) {
    mp_wasm_trust_ensure();
    if (bytes == NULL || len == 0) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "verify: empty");
        }
        return false;
    }

    const uint8_t *sec_payload = NULL;
    uint32_t sec_payload_len = 0;
    if (!mp_wasm_sig_find(bytes, len, &sec_payload, &sec_payload_len)) {
        #if MICROPY_WASM_VERIFY == 1
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "verify: signature required");
        }
        return false;
        #else
        return true;
        #endif
    }

    uint8_t *stripped = NULL;
    uint32_t stripped_len = 0;
    if (!mp_wasm_sig_strip(bytes, len, &stripped, &stripped_len)) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "verify: bad wasmmod.sig layout");
        }
        return false;
    }

    mp_wasm_sig_info_t info;
    if (!mp_wasm_sig_parse(sec_payload, sec_payload_len, &info)) {
        MICROPY_WASM_FREE(stripped);
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "verify: bad wasmmod.sig payload");
        }
        return false;
    }

    bool ok = verify_with_trust(stripped, stripped_len, info.sig, info.sig_len, info.chain, info.chain_len);
    MICROPY_WASM_FREE(stripped);
    if (!ok) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "verify: bad signature");
        }
        return false;
    }
    return true;
}

#else // !MICROPY_WASM_VERIFY

bool mp_wasm_sig_find(const uint8_t *bytes, uint32_t len, const uint8_t **payload, uint32_t *payload_len) {
    (void)bytes;
    (void)len;
    (void)payload;
    (void)payload_len;
    return false;
}

bool mp_wasm_sig_parse(const uint8_t *payload, uint32_t payload_len, mp_wasm_sig_info_t *out) {
    (void)payload;
    (void)payload_len;
    (void)out;
    return false;
}

bool mp_wasm_sig_strip(const uint8_t *bytes, uint32_t len, uint8_t **out, uint32_t *out_len) {
    (void)bytes;
    (void)len;
    (void)out;
    (void)out_len;
    return false;
}

bool mp_wasm_verify_bytes(const uint8_t *bytes, uint32_t len, const char *path_hint, char *errbuf, size_t errbuf_len) {
    (void)bytes;
    (void)len;
    (void)path_hint;
    (void)errbuf;
    (void)errbuf_len;
    (void)verify_runtime_enabled; // still settable; compile-time off = always allow
    return true;
}

bool mp_wasm_verify_sig(const uint8_t *bytes, uint32_t len, char *errbuf, size_t errbuf_len) {
    (void)bytes;
    (void)len;
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len, "verify: disabled");
    }
    return false;
}

#endif // MICROPY_WASM_VERIFY

#endif // MICROPY_PY_WASM
