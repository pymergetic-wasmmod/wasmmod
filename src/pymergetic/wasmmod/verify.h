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
#ifndef PYMERGETIC_WASMMOD_VERIFY_H
#define PYMERGETIC_WASMMOD_VERIFY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// MICROPY_WASM_VERIFY:
//   0 = off (default)
//   1 = require a valid wasmmod.sig for every load
//   2 = verify when wasmmod.sig present; otherwise allow
#ifndef MICROPY_WASM_VERIFY
#define MICROPY_WASM_VERIFY (0)
#endif

// Embedded section envelope: MPWS + ver + flags + sig + chain (leaf first).
// Chain DER = leaf || intermediates (root stays in host trust store).
typedef struct mp_wasm_sig_info_t {
    const uint8_t *sig;
    uint32_t sig_len;
    const uint8_t *chain;
    uint32_t chain_len;
    bool is_mpws;
} mp_wasm_sig_info_t;

// Find wasmmod.sig custom section payload (.wasm or .aot).
bool mp_wasm_sig_find(const uint8_t *bytes, uint32_t len, const uint8_t **payload, uint32_t *payload_len);
// Parse MPWS or raw ECDSA payload (pointers into payload).
bool mp_wasm_sig_parse(const uint8_t *payload, uint32_t payload_len, mp_wasm_sig_info_t *out);
// Copy artifact omitting wasmmod.sig (hash input). Caller MICROPY_WASM_FREE(*out).
bool mp_wasm_sig_strip(const uint8_t *bytes, uint32_t len, uint8_t **out, uint32_t *out_len);

// Add trust material: CA cert DER/PEM (PKI) and/or leaf SPKI DER (pinned pubkey).
bool mp_wasm_trust_add(const uint8_t *key, size_t key_len);
void mp_wasm_trust_clear(void); // clears store; disables baked-CA auto-reload this session
size_t mp_wasm_trust_count(void); // may lazy-load baked roots first

// Session start (wasm.__init__): empty store, arm lazy baked-CA load.
void mp_wasm_trust_init_session(void);
// Ensure baked roots are loaded (no-op if disarmed / already loaded / none linked).
void mp_wasm_trust_ensure(void);

// Load compile-time / image-baked trust anchors (weak no-op unless linked).
// Prefer calling via mp_wasm_trust_ensure(), not directly.
void mp_wasm_trust_load_builtin(void);

// Helper for generated wasm_trust_ca.c: add one root from zlib (or raw if lens equal).
bool mp_wasm_trust_add_blob(const uint8_t *data, uint32_t data_len, uint32_t uncompressed_len);

// Runtime verify gate (default on). When false, mp_wasm_verify_bytes is a no-op.
// Compile-time MICROPY_WASM_VERIFY==0 remains a full no-op regardless.
void mp_wasm_set_verify_enabled(bool enabled);
bool mp_wasm_get_verify_enabled(void);

// Verify embedded wasmmod.sig before instantiate. path_hint is for errors only (may be NULL).
// Honors the session gate (mp_wasm_set_verify_enabled). On failure fills errbuf and returns false.
bool mp_wasm_verify_bytes(const uint8_t *bytes, uint32_t len, const char *path_hint, char *errbuf, size_t errbuf_len);

// Explicit check of embedded wasmmod.sig (ignores session gate). Same crypto as load-time verify.
bool mp_wasm_verify_sig(const uint8_t *bytes, uint32_t len, char *errbuf, size_t errbuf_len);

// ---------------------------------------------------------------------------
// Trust policy (sub-CA allow/deny revocation bundle, MPTB)
//
// A revocation *bundle* is a small signed document fetched (e.g. from the CDN)
// once per session. Its signature chains to a baked root via the same
// verify_pki_chain path a pack uses, so no second trust root is needed. When
// applied it installs:
//   allow[]  sub-CA SHA-256 fingerprints that may sign packs (empty = any)
//   deny[]   sub-CA SHA-256 fingerprints that must be rejected
// After a bundle is applied, a pack's *issuing sub-CA* (the cert that signed
// its leaf, i.e. the intermediate below the root) must be allowed and not
// denied. This is the revocation mechanism for the Root -> sub-CA -> leaf
// hierarchy (Decision 1b: signed by a dedicated revocation sub-CA under root).
// ---------------------------------------------------------------------------

// Fixed-size SHA-256 sub-CA fingerprint (DER cert digest, same convention as
// the CDN TrustService sha256 field).
#define MP_WASM_TRUST_FP_LEN 32u

typedef struct mp_wasm_trust_bundle_t {
    uint64_t issued;    // unix seconds (load order guard; informational)
    uint64_t expires;   // unix seconds; bundle invalid after this
    const uint8_t *allow;   // MP_WASM_TRUST_FP_LEN * n_allow bytes
    uint32_t n_allow;
    const uint8_t *deny;    // MP_WASM_TRUST_FP_LEN * n_deny bytes
    uint32_t n_deny;
} mp_wasm_trust_bundle_t;

// Parse the MPTB payload (pointers alias the payload; do not free separately).
// Returns true on a well-formed envelope. On success *out_covered_len is the
// number of bytes the bundle signature covers (data before sig_len). Signature
// and signer-chain authentication happen in apply.
bool mp_wasm_trust_bundle_parse(const uint8_t *payload, uint32_t len,
                                mp_wasm_trust_bundle_t *out,
                                const uint8_t **sig, uint32_t *sig_len,
                                const uint8_t **signer_chain, uint32_t *signer_chain_len,
                                uint32_t *out_covered_len,
                                char *errbuf, size_t errbuf_len);

// Authenticate + install a bundle. Verifies the bundle signature chains to a
// baked root (verify_pki_chain), checks it is not expired, then replaces the
// current allow/deny policy. Fails closed: on any crypto/format failure the
// existing policy is left untouched (or cleared if bootstrap).
bool mp_wasm_trust_apply_bundle(const uint8_t *payload, uint32_t len,
                                char *errbuf, size_t errbuf_len);

// Clear the installed policy (allow = any, deny = none). Called on session init.
void mp_wasm_trust_policy_reset(void);

// 1 if a policy has been applied this session (bundle accepted). 0 = none.
bool mp_wasm_trust_policy_applied(void);

// Counters for diagnostics: number of allow / deny entries installed.
uint32_t mp_wasm_trust_policy_allow_count(void);
uint32_t mp_wasm_trust_policy_deny_count(void);

#endif // PYMERGETIC_WASMMOD_VERIFY_H
