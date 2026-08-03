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
#include "extmod/wasmmod/pack.h"

#if MICROPY_SSL_MBEDTLS
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#endif

static bool load_detached_sig(const char *path_hint, vstr_t *sig_out) {
    if (path_hint == NULL || mp_wasm_uri_is_http(path_hint)) {
        return false;
    }
    vstr_t sig_path;
    vstr_init(&sig_path, strlen(path_hint) + 5);
    vstr_add_str(&sig_path, path_hint);
    vstr_add_str(&sig_path, ".sig");
    char err[64];
    bool ok = mp_wasm_fetch(vstr_null_terminated_str(&sig_path), sig_out, err, sizeof(err));
    vstr_clear(&sig_path);
    return ok;
}

static bool load_section_sig(const uint8_t *bytes, uint32_t len, const uint8_t **sig, uint32_t *sig_len) {
    return mp_wasm_find_custom_section(bytes, len, MP_WASM_SIG_SECTION, sig, sig_len);
}

#if MICROPY_SSL_MBEDTLS
static bool verify_ecdsa_sha256(const uint8_t *bytes, uint32_t len, const uint8_t *sig, uint32_t sig_len, const uint8_t *key, size_t key_len) {
    unsigned char hash[32];
    if (mbedtls_sha256(bytes, len, hash, 0) != 0) {
        return false;
    }
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_parse_public_key(&pk, key, key_len);
    bool ok = false;
    if (ret == 0 && mbedtls_pk_can_do(&pk, MBEDTLS_PK_ECDSA)) {
        ok = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sig_len) == 0;
    }
    mbedtls_pk_free(&pk);
    return ok;
}
#endif

static bool verify_with_trust(const uint8_t *bytes, uint32_t len, const uint8_t *sig, uint32_t sig_len) {
    #ifdef MICROPY_WASM_VERIFY_HOOK
    return MICROPY_WASM_VERIFY_HOOK(bytes, len, sig, sig_len);
    #elif MICROPY_SSL_MBEDTLS
    if (trust_n == 0) {
        return false;
    }
    for (mp_wasm_trust_key_t *k = trust_keys; k != NULL; k = k->next) {
        if (verify_ecdsa_sha256(bytes, len, sig, sig_len, k->key, k->key_len)) {
            return true;
        }
    }
    return false;
    #else
    (void)bytes;
    (void)len;
    (void)sig;
    (void)sig_len;
    return false;
    #endif
}

bool mp_wasm_verify_bytes(const uint8_t *bytes, uint32_t len, const char *path_hint, char *errbuf, size_t errbuf_len) {
    if (bytes == NULL || len == 0) {
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "verify: empty");
        }
        return false;
    }

    vstr_t detached;
    bool have_detached = load_detached_sig(path_hint, &detached);
    const uint8_t *sig = NULL;
    uint32_t sig_len = 0;
    const uint8_t *sec_sig = NULL;
    uint32_t sec_len = 0;
    bool have_section = load_section_sig(bytes, len, &sec_sig, &sec_len);

    if (have_detached) {
        sig = (const uint8_t *)detached.buf;
        sig_len = (uint32_t)detached.len;
    } else if (have_section) {
        sig = sec_sig;
        sig_len = sec_len;
    } else {
        #if MICROPY_WASM_VERIFY == 1
        if (errbuf && errbuf_len) {
            snprintf(errbuf, errbuf_len, "verify: signature required");
        }
        return false;
        #else
        return true;
        #endif
    }

    bool ok = verify_with_trust(bytes, len, sig, sig_len);
    if (have_detached) {
        vstr_clear(&detached);
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
    return true;
}

#endif // MICROPY_WASM_VERIFY

#endif // MICROPY_PY_WASM
