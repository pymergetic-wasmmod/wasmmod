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

#if MICROPY_PY_WASM || (defined(PM_WASMMOD_CPYTHON) && PM_WASMMOD_CPYTHON)

#include <stdio.h>
#include <string.h>

#include "py/mpconfig.h"
#include "pymergetic/wasmmod/verify.h"

#include "pymergetic/wasmmod/pack/alloc.h"
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

// --- Trust policy (MPTB allow/deny by sub-CA fingerprint) -----------------
// Fixed-size fingerprint arrays; a policy only takes effect after a bundle
// has been accepted (mp_wasm_trust_apply_bundle). Empty allow = any trusted
// sub-CA; a denied sub-CA always fails regardless of allow.
static uint8_t *policy_allow;
static uint32_t policy_allow_n;
static uint8_t *policy_deny;
static uint32_t policy_deny_n;
static bool policy_applied;

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
    mp_wasm_trust_policy_reset();
}

void mp_wasm_trust_policy_reset(void) {
    if (policy_allow != NULL) {
        MICROPY_WASM_FREE(policy_allow);
        policy_allow = NULL;
    }
    if (policy_deny != NULL) {
        MICROPY_WASM_FREE(policy_deny);
        policy_deny = NULL;
    }
    policy_allow_n = 0;
    policy_deny_n = 0;
    policy_applied = false;
}

bool mp_wasm_trust_policy_applied(void) {
    return policy_applied;
}

uint32_t mp_wasm_trust_policy_allow_count(void) {
    return policy_allow_n;
}

uint32_t mp_wasm_trust_policy_deny_count(void) {
    return policy_deny_n;
}

// Bundle wire format (big-endian), see sign.py `trust-bundle` for the twin:
//   offset 0 : "MPTB"
//   4        : u16 version = 1
//   6        : u16 type = 1 (TRUST)
//   8        : u64 issued (unix secs)
//   16       : u64 expires (unix secs)
//   24       : u16 n_allow
//   26       : u16 n_deny
//   28       : allow  fps, 32 bytes each
//   ...      : deny   fps, 32 bytes each
//   ...      : u16 sig_len,  sig (ECDSA-P256 raw r||s)
//   ...      : u32 chain_len, chain (DER certs leaf-first: leaf + revocation
//                                   sub-CA + optional root)
// Signature covers all bytes from "MPTB" up to (excluding) the sig_len field.
#define MP_WASM_TRUST_BUNDLE_MAGIC "MPTB"
#define MP_WASM_TRUST_BUNDLE_VER 1u
#define MP_WASM_TRUST_BUNDLE_TYPE_TRUST 1u

bool mp_wasm_trust_bundle_parse(const uint8_t *payload, uint32_t len,
                                mp_wasm_trust_bundle_t *out,
                                const uint8_t **sig, uint32_t *sig_len,
                                const uint8_t **signer_chain, uint32_t *signer_chain_len,
                                uint32_t *out_covered_len,
                                char *errbuf, size_t errbuf_len) {
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
    if (sig != NULL) {
        *sig = NULL;
    }
    if (sig_len != NULL) {
        *sig_len = 0;
    }
    if (signer_chain != NULL) {
        *signer_chain = NULL;
    }
    if (signer_chain_len != NULL) {
        *signer_chain_len = 0;
    }
    if (out_covered_len != NULL) {
        *out_covered_len = 0;
    }
    if (payload == NULL || out == NULL || sig == NULL || sig_len == NULL
        || signer_chain == NULL || signer_chain_len == NULL || len < 28
        || memcmp(payload, MP_WASM_TRUST_BUNDLE_MAGIC, 4) != 0) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "trust: bad bundle header");
        }
        return false;
    }
    uint32_t off = 4;
    uint16_t ver = ((uint16_t)payload[off] << 8) | payload[off + 1];
    off += 2;
    uint16_t type = ((uint16_t)payload[off] << 8) | payload[off + 1];
    off += 2;
    if (ver != MP_WASM_TRUST_BUNDLE_VER || type != MP_WASM_TRUST_BUNDLE_TYPE_TRUST) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "trust: unsupported bundle version/type");
        }
        return false;
    }
    if (len < 28) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "trust: truncated bundle");
        }
        return false;
    }
    uint64_t issued = 0, expires = 0;
    for (int b = 0; b < 8; b++) {
        issued = (issued << 8) | payload[off + b];
    }
    off += 8;
    for (int b = 0; b < 8; b++) {
        expires = (expires << 8) | payload[off + b];
    }
    off += 8;
    uint32_t n_allow = ((uint32_t)payload[off] << 8) | payload[off + 1];
    off += 2;
    uint32_t n_deny = ((uint32_t)payload[off] << 8) | payload[off + 1];
    off += 2;

    // Allow + deny fingerprints.
    if (n_allow > 0) {
        size_t bytes = (size_t)n_allow * MP_WASM_TRUST_FP_LEN;
        if (bytes > (size_t)(len - off)) {
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "trust: truncated allow list");
            }
            return false;
        }
        out->allow = payload + off;
        out->n_allow = n_allow;
        off += (uint32_t)bytes;
    }
    if (n_deny > 0) {
        size_t bytes = (size_t)n_deny * MP_WASM_TRUST_FP_LEN;
        if (bytes > (size_t)(len - off)) {
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "trust: truncated deny list");
            }
            return false;
        }
        out->deny = payload + off;
        out->n_deny = n_deny;
        off += (uint32_t)bytes;
    }

    // Signature.
    if (off + 2 > len) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "trust: missing signature");
        }
        return false;
    }
    if (out_covered_len != NULL) {
        *out_covered_len = off;
    }
    uint32_t sl = ((uint32_t)payload[off] << 8) | payload[off + 1];
    off += 2;
    if (sl == 0 || off + sl > len) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "trust: bad signature length");
        }
        return false;
    }
    *sig = payload + off;
    *sig_len = sl;
    off += sl;

    // Signer chain (length-prefixed DER, leaf-first).
    if (off + 4 > len) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "trust: missing signer chain");
        }
        return false;
    }
    uint32_t cl = 0;
    for (int b = 0; b < 4; b++) {
        cl = (cl << 8) | payload[off + b];
    }
    off += 4;
    if (cl == 0 || off + cl > len) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "trust: bad signer chain length");
        }
        return false;
    }
    *signer_chain = payload + off;
    *signer_chain_len = cl;

    out->issued = issued;
    out->expires = expires;
    return true;
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
#include "pymergetic/wasmmod/pack/zlib_env.h"
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

#include "pymergetic/wasmmod/pack/__types__.h"
#include "pymergetic/wasmmod/pack/format/common/format.h"

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

bool mp_wasm_sig_strip(const uint8_t *mod, uint32_t len, uint8_t **out, uint32_t *out_len) {
    return mp_wasm_format_strip_section(mod, len, MP_WASM_SIG_SECTION, out, out_len);
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

// Fingerprint a cert (SHA-256 over its DER bytes), same convention as the
// CDN TrustService sha256 field. Writes 32 bytes to fp.
static bool cert_sha256_fp(mbedtls_x509_crt *cert, uint8_t fp[MP_WASM_TRUST_FP_LEN]) {
    if (cert == NULL || cert->raw.p == NULL || cert->raw.len == 0) {
        return false;
    }
    return mbedtls_sha256(cert->raw.p, cert->raw.len, fp, 0) == 0;
}

static bool fp_in_list(const uint8_t *fp, const uint8_t *list, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (memcmp(fp, list + (size_t)i * MP_WASM_TRUST_FP_LEN, MP_WASM_TRUST_FP_LEN) == 0) {
            return true;
        }
    }
    return false;
}

// Sub-CA policy gate (revocation). A successfully chain-verified pack is
// accepted only if its *issuing sub-CA* is allowed and not denied.
//
//   - No policy applied: pass through (back-compat; verify is identity-only).
//   - Policy applied:
//       deny wins:  denied sub-CA => fail closed.
//       allow list non-empty: sub-CA must be listed.
//       no intermediate under the leaf: reject (not issued under a listed
//         sub-CA; only reached when a policy is actually enforcing).
static bool policy_ok_for_chain(mbedtls_x509_crt *leaf) {
    if (!policy_applied) {
        return true;
    }
    // The sub-CA is the cert that issued the leaf, i.e. the first
    // intermediate. If the leaf chains straight to a root there is no
    // recoverable sub-CA identity to scope on -> reject under policy.
    mbedtls_x509_crt *subca = leaf->next;
    if (subca == NULL) {
        return false;
    }
    uint8_t fp[MP_WASM_TRUST_FP_LEN];
    if (!cert_sha256_fp(subca, fp)) {
        return false;
    }
    if (policy_deny_n > 0 && fp_in_list(fp, policy_deny, policy_deny_n)) {
        return false;
    }
    if (policy_allow_n > 0 && !fp_in_list(fp, policy_allow, policy_allow_n)) {
        return false;
    }
    return true;
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

    if (!policy_ok_for_chain(&leaf)) {
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
            snprintf(errbuf, errbuf_len,
                "verify: signature required (len=%u magic=%02x%02x%02x%02x)",
                (unsigned)len,
                len > 0 ? bytes[0] : 0, len > 1 ? bytes[1] : 0,
                len > 2 ? bytes[2] : 0, len > 3 ? bytes[3] : 0);
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

bool mp_wasm_trust_apply_bundle(const uint8_t *payload, uint32_t len, char *errbuf, size_t errbuf_len) {
    mp_wasm_trust_bundle_t b;
    const uint8_t *sig;
    uint32_t sig_len;
    const uint8_t *chain;
    uint32_t chain_len;
    uint32_t covered;
    if (!mp_wasm_trust_bundle_parse(payload, len, &b, &sig, &sig_len, &chain, &chain_len,
            &covered, errbuf, errbuf_len)) {
        return false;
    }

    // Expiry / not-yet-valid check (informational). Bare-metal metal has no
    // trusted wall clock, so the device does not gate on timestamps; the CDN /
    // boot policy enforces freshness by serving only the current bundle.
    (void)b.issued;
    (void)b.expires;

    // Authenticate the bundle: its signature covers [0, covered), and the
    // signer leaf + revocation sub-CA must chain to a baked root. This reuses
    // the pack verify path (includes the sub-CA allow/deny gate, so a bundle
    // signed by a revoked sub-CA is itself rejected).
#if MICROPY_SSL_MBEDTLS
    if (!verify_pki_chain(payload, covered, sig, sig_len, chain, chain_len)) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "trust: bundle signature failed");
        }
        return false;
    }
#else
    (void)covered;
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len, "trust: bundle verify requires mbedtls");
    }
    return false;
#endif

    // Install the policy (copy fingerprints out of the alias'd payload).
    uint8_t *new_allow = NULL;
    uint8_t *new_deny = NULL;
    if (b.n_allow > 0) {
        size_t bytes = (size_t)b.n_allow * MP_WASM_TRUST_FP_LEN;
        new_allow = MICROPY_WASM_MALLOC(bytes);
        if (new_allow == NULL) {
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "trust: OOM allow");
            }
            return false;
        }
        memcpy(new_allow, b.allow, bytes);
    }
    if (b.n_deny > 0) {
        size_t bytes = (size_t)b.n_deny * MP_WASM_TRUST_FP_LEN;
        new_deny = MICROPY_WASM_MALLOC(bytes);
        if (new_deny == NULL) {
            MICROPY_WASM_FREE(new_allow);
            if (errbuf && errbuf_len) {
                snprintf(errbuf, errbuf_len, "trust: OOM deny");
            }
            return false;
        }
        memcpy(new_deny, b.deny, bytes);
    }

    if (policy_allow != NULL) {
        MICROPY_WASM_FREE(policy_allow);
    }
    if (policy_deny != NULL) {
        MICROPY_WASM_FREE(policy_deny);
    }
    policy_allow = new_allow;
    policy_allow_n = b.n_allow;
    policy_deny = new_deny;
    policy_deny_n = b.n_deny;
    policy_applied = true;
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

bool mp_wasm_trust_apply_bundle(const uint8_t *payload, uint32_t len, char *errbuf, size_t errbuf_len) {
    (void)payload;
    (void)len;
    if (errbuf && errbuf_len) {
        snprintf(errbuf, errbuf_len, "trust: verify disabled at build");
    }
    return false;
}

#endif // MICROPY_WASM_VERIFY

#endif // MICROPY_PY_WASM || PM_WASMMOD_CPYTHON
