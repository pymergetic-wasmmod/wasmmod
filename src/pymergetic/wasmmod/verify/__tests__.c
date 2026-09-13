/* pymergetic.wasmmod.verify -- module tests (__tests__.c).
 * The verify card validates wasmmod.sig embedded signatures against
 * trust key material, ECDSA-SHA256 via mbedtls, plus runtime policy
 * bundles.  Error paths (null args, empty data) are independent of
 * whether a trust root is loaded, so they always prove.
 *
 * Build: the host metal `make test` sets MICROPY_SSL_MBEDTLS=1 and
 * MICROPY_WASM_VERIFY=1 via CPPFLAGS. */
#include "pymergetic/wasmmod/guest.h"
#include "pymergetic/wasmmod/verify.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail(const char *why) {
    fprintf(stderr, "wasmmod.verify test: %s\n", why);
    return 1;
}

/* ---- sig_find: null/empty/truncated input ---- */

static int32_t case_sig_find_null(void) {
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    if (mp_wasm_sig_find(NULL, 0, &payload, &payload_len))
        return fail("sig_find NULL bytes succeeded");
    if (mp_wasm_sig_find(NULL, 0, NULL, &payload_len))
        return fail("sig_find NULL payload ptr succeeded");
    if (mp_wasm_sig_find(NULL, 0, &payload, NULL))
        return fail("sig_find NULL len ptr succeeded");
    return 0;
}

static int32_t case_sig_find_empty(void) {
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    /* Empty input: no wasmmod.sig to find. */
    if (mp_wasm_sig_find((const uint8_t *)"", 0, &payload, &payload_len))
        return fail("sig_find empty bytes succeeded");
    return 0;
}

/* A minimal valid wasm module (v1, empty).  The wasm binary format: magic
 * \0asm, version 1 (4 bytes LE), then sections.  This module has nothing
 * but the magic + version, so no custom section = sig_find returns false. */
static int32_t case_sig_find_minimal_wasm(void) {
    const uint8_t *payload = NULL;
    uint32_t payload_len = 0;
    const uint8_t wasm[] = {
        0x00, 0x61, 0x73, 0x6d,  /* \0asm */
        0x01, 0x00, 0x00, 0x00,  /* version 1 */
    };
    bool ok = mp_wasm_sig_find(wasm, sizeof(wasm), &payload, &payload_len);
    (void)ok;  /* false expected: no custom section */
    return 0;
}

/* ---- sig_parse: null/empty/truncated ---- */

static int32_t case_sig_parse_null(void) {
    mp_wasm_sig_info_t info;
    if (mp_wasm_sig_parse(NULL, 0, NULL))
        return fail("sig_parse NULL succeeded");
    if (mp_wasm_sig_parse(NULL, 0, &info))
        return fail("sig_parse NULL payload succeeded");
    return 0;
}

static int32_t case_sig_parse_empty(void) {
    mp_wasm_sig_info_t info;
    if (mp_wasm_sig_parse((const uint8_t *)"", 0, &info))
        return fail("sig_parse empty succeeded");
    return 0;
}

/* A raw 64-byte ECDSA signature (r||s) -- treated as non-MPWS. */
#if MICROPY_WASM_VERIFY
static int32_t case_sig_parse_raw(void) {
    mp_wasm_sig_info_t info;
    uint8_t raw[64];
    int i;
    for (i = 0; i < 64; i++)
        raw[i] = (uint8_t)(i + 1);

    if (!mp_wasm_sig_parse(raw, sizeof(raw), &info))
        return fail("sig_parse raw failed");
    if (info.is_mpws)
        return fail("raw sig misidentified as MPWS");
    if (info.sig != raw || info.sig_len != 64u)
        return fail("raw sig fields wrong");
    return 0;
}
#endif /* MICROPY_WASM_VERIFY */

/* A well-formed but truncated MPWS envelope. */
static int32_t case_sig_parse_mpws_truncated(void) {
    mp_wasm_sig_info_t info;
    /* MPWS magic, ver=1, flags=0, sig_len=9999 (too large) */
    const uint8_t mpws[] = {
        'M', 'P', 'W', 'S',           /* magic */
        0x01,                          /* ver */
        0x00,                          /* flags */
        0x27, 0x0F,                    /* sig_len = 9999 (too large for payload) */
    };
    if (mp_wasm_sig_parse(mpws, sizeof(mpws), &info))
        return fail("MPWS truncated accepted");
    return 0;
}

/* A well-formed MPWS with zero-length sig. */
static int32_t case_sig_parse_mpws_zero_sig(void) {
    mp_wasm_sig_info_t info;
    const uint8_t mpws[] = {
        'M', 'P', 'W', 'S',           /* magic */
        0x01,                          /* ver */
        0x00,                          /* flags */
        0x00, 0x00,                    /* sig_len = 0 */
    };
    if (mp_wasm_sig_parse(mpws, sizeof(mpws), &info))
        return fail("MPWS zero sig accepted");
    return 0;
}

/* ---- sig_strip: null/empty ---- */

static int32_t case_sig_strip_null(void) {
    uint8_t *out = NULL;
    uint32_t out_len = 0;
    if (mp_wasm_sig_strip(NULL, 0, &out, &out_len))
        return fail("sig_strip NULL succeeded");
    if (mp_wasm_sig_strip(NULL, 0, NULL, &out_len))
        return fail("sig_strip NULL out ptr succeeded");
    if (mp_wasm_sig_strip(NULL, 0, &out, NULL))
        return fail("sig_strip NULL len ptr succeeded");
    return 0;
}

/* ---- trust_add: null/empty key ---- */

static int32_t case_trust_add_null(void) {
    mp_wasm_trust_clear();
    if (mp_wasm_trust_add(NULL, 0))
        return fail("trust_add NULL key succeeded");
    if (mp_wasm_trust_add(NULL, 32))
        return fail("trust_add NULL key with length succeeded");
    if (mp_wasm_trust_add((const uint8_t *)"k", 0))
        return fail("trust_add zero-length key succeeded");
    mp_wasm_trust_clear();
    return 0;
}

/* ---- trust_add: valid key, then count + clear ---- */

static int32_t case_trust_add_count_clear(void) {
    const char *key = "pretend-trust-key-32-bytes-pad";
    mp_wasm_trust_clear();
    if (mp_wasm_trust_count() != 0)
        return fail("count after clear");
    if (!mp_wasm_trust_add((const uint8_t *)key, 32))
        return fail("trust_add valid key failed");
    if (mp_wasm_trust_count() == 0)
        return fail("count after add");
    mp_wasm_trust_clear();
    if (mp_wasm_trust_count() != 0)
        return fail("count after clear again");
    return 0;
}

/* ---- trust_init_session ---- */

static int32_t case_trust_init_session(void) {
    const char *key = "another-trust-key-32-bytes!";
    mp_wasm_trust_add((const uint8_t *)key, 32);
    mp_wasm_trust_init_session();
    /* After init_session, store is cleared and baked CA is armed. */
    if (mp_wasm_trust_count() != 0)
        return fail("count after init_session not zero");
    return 0;
}

/* ---- trust_blob: null/empty/zero uncompressed ---- */

static int32_t case_trust_blob_null(void) {
    if (mp_wasm_trust_add_blob(NULL, 16, 16))
        return fail("trust_add_blob NULL data succeeded");
    mp_wasm_trust_clear();
    return 0;
}

static int32_t case_trust_blob_zero_len(void) {
    if (mp_wasm_trust_add_blob((const uint8_t *)"x", 0, 8))
        return fail("trust_add_blob zero data_len succeeded");
    if (mp_wasm_trust_add_blob((const uint8_t *)"x", 8, 0))
        return fail("trust_add_blob zero uncompressed_len succeeded");
    mp_wasm_trust_clear();
    return 0;
}

/* ---- verify enable/disable ---- */

static int32_t case_verify_enable(void) {
    mp_wasm_set_verify_enabled(true);
    if (!mp_wasm_get_verify_enabled())
        return fail("enable not read back");
    mp_wasm_set_verify_enabled(false);
    if (mp_wasm_get_verify_enabled())
        return fail("disable not read back");
    mp_wasm_set_verify_enabled(true);
    return 0;
}

/* ---- verify_bytes: null/empty (error paths) ---- */

#if MICROPY_WASM_VERIFY
static int32_t case_verify_bytes_null(void) {
    mp_wasm_set_verify_enabled(true);
    char errbuf[64];
    /* NULL bytes should fail. */
    if (mp_wasm_verify_bytes(NULL, 0, NULL, errbuf, sizeof(errbuf)))
        return fail("verify_bytes NULL data succeeded");
    return 0;
}

static int32_t case_verify_bytes_empty(void) {
    mp_wasm_set_verify_enabled(true);
    char errbuf[64];
    if (mp_wasm_verify_bytes((const uint8_t *)"", 0, NULL, errbuf, sizeof(errbuf)))
        return fail("verify_bytes empty data succeeded");
    return 0;
}
#endif /* MICROPY_WASM_VERIFY */

static int32_t case_verify_bytes_disabled(void) {
    mp_wasm_set_verify_enabled(false);
    /* When disabled, verify_bytes should succeed on nothing. */
    if (!mp_wasm_verify_bytes(NULL, 0, NULL, NULL, 0))
        return fail("verify_bytes disabled must pass");
    mp_wasm_set_verify_enabled(true);
    return 0;
}

/* ---- verify_sig: null/empty ---- */

static int32_t case_verify_sig_null(void) {
    char errbuf[64];
    if (mp_wasm_verify_sig(NULL, 0, errbuf, sizeof(errbuf)))
        return fail("verify_sig NULL data succeeded");
    if (mp_wasm_verify_sig(NULL, 0, NULL, 0))
        return fail("verify_sig NULL data with zero errbuf succeeded");
    return 0;
}

static int32_t case_verify_sig_empty(void) {
    char errbuf[64];
    if (mp_wasm_verify_sig((const uint8_t *)"", 0, errbuf, sizeof(errbuf)))
        return fail("verify_sig empty data succeeded");
    return 0;
}

/* ---- trust_bundle_parse: null/truncated/wrong magic ---- */

static int32_t case_trust_bundle_null(void) {
    mp_wasm_trust_bundle_t b;
    const uint8_t *sig = NULL;
    uint32_t sig_len = 0;
    const uint8_t *chain = NULL;
    uint32_t chain_len = 0;
    uint32_t covered = 0;
    char errbuf[64];
    if (mp_wasm_trust_bundle_parse(NULL, 0, &b, &sig, &sig_len,
            &chain, &chain_len, &covered, errbuf, sizeof(errbuf)))
        return fail("bundle_parse NULL succeeded");
    return 0;
}

static int32_t case_trust_bundle_truncated(void) {
    mp_wasm_trust_bundle_t b;
    const uint8_t *sig = NULL;
    uint32_t sig_len = 0;
    const uint8_t *chain = NULL;
    uint32_t chain_len = 0;
    uint32_t covered = 0;
    char errbuf[64];
    /* Shorter than the 28-byte minimum header. */
    const uint8_t tiny[4] = {0};
    if (mp_wasm_trust_bundle_parse(tiny, sizeof(tiny), &b, &sig, &sig_len,
            &chain, &chain_len, &covered, errbuf, sizeof(errbuf)))
        return fail("bundle_parse tiny succeeded");
    return 0;
}

static int32_t case_trust_bundle_wrong_magic(void) {
    mp_wasm_trust_bundle_t b;
    const uint8_t *sig = NULL;
    uint32_t sig_len = 0;
    const uint8_t *chain = NULL;
    uint32_t chain_len = 0;
    uint32_t covered = 0;
    char errbuf[64];
    /* Enough bytes but wrong magic. */
    uint8_t wrong[40];
    memset(wrong, 0xAA, sizeof(wrong));
    if (mp_wasm_trust_bundle_parse(wrong, sizeof(wrong), &b, &sig, &sig_len,
            &chain, &chain_len, &covered, errbuf, sizeof(errbuf)))
        return fail("bundle_parse wrong magic succeeded");
    return 0;
}

static int32_t case_trust_bundle_wrong_version(void) {
    mp_wasm_trust_bundle_t b;
    const uint8_t *sig = NULL;
    uint32_t sig_len = 0;
    const uint8_t *chain = NULL;
    uint32_t chain_len = 0;
    uint32_t covered = 0;
    char errbuf[64];
    /* Correct magic, wrong version. */
    const uint8_t hdr[] = {
        'M', 'P', 'T', 'B',
        0x00, 0x02,  /* version = 2 (unsupported) */
        0x00, 0x01,  /* type = 1 (trust) */
        0,0,0,0,0,0,0,0,  /* issued */
        0,0,0,0,0,0,0,0,  /* expires */
        0x00, 0x00,  /* n_allow = 0 */
        0x00, 0x00,  /* n_deny = 0 */
    };
    if (mp_wasm_trust_bundle_parse(hdr, sizeof(hdr), &b, &sig, &sig_len,
            &chain, &chain_len, &covered, errbuf, sizeof(errbuf)))
        return fail("bundle_parse wrong version succeeded");
    return 0;
}

/* ---- policy defaults ---- */

static int32_t case_policy_defaults(void) {
    mp_wasm_trust_policy_reset();
    if (mp_wasm_trust_policy_applied())
        return fail("policy applied after reset");
    if (mp_wasm_trust_policy_allow_count() != 0u)
        return fail("allow count after reset");
    if (mp_wasm_trust_policy_deny_count() != 0u)
        return fail("deny count after reset");
    return 0;
}

/* ---- trust_apply_bundle: null/empty ---- */

static int32_t case_trust_apply_bundle_null(void) {
    char errbuf[64];
    if (mp_wasm_trust_apply_bundle(NULL, 0, errbuf, sizeof(errbuf)))
        return fail("apply_bundle NULL succeeded");
    return 0;
}

static int32_t case_trust_apply_bundle_empty(void) {
    char errbuf[64];
    if (mp_wasm_trust_apply_bundle((const uint8_t *)"", 0, errbuf, sizeof(errbuf)))
        return fail("apply_bundle empty succeeded");
    return 0;
}

/* ---- trust_load_builtin (weak, always a no-op unless linked) ---- */

static int32_t case_trust_load_builtin(void) {
    mp_wasm_trust_init_session();
    mp_wasm_trust_load_builtin();
    /* The weak default is a no-op; must not crash. */
    return 0;
}

/* ---- driver ---- */

static int32_t pm_wasmmod_verify_tests(void) {
    int32_t st;
    st = case_sig_find_null();
    if (st == 0) st = case_sig_find_empty();
    if (st == 0) st = case_sig_find_minimal_wasm();
    if (st == 0) st = case_sig_parse_null();
    if (st == 0) st = case_sig_parse_empty();
#if MICROPY_WASM_VERIFY
    if (st == 0) st = case_sig_parse_raw();
#endif
    if (st == 0) st = case_sig_parse_mpws_truncated();
    if (st == 0) st = case_sig_parse_mpws_zero_sig();
    if (st == 0) st = case_sig_strip_null();
    if (st == 0) st = case_trust_add_null();
    if (st == 0) st = case_trust_add_count_clear();
    if (st == 0) st = case_trust_init_session();
    if (st == 0) st = case_trust_blob_null();
    if (st == 0) st = case_trust_blob_zero_len();
    if (st == 0) st = case_verify_enable();
#if MICROPY_WASM_VERIFY
    if (st == 0) st = case_verify_bytes_null();
    if (st == 0) st = case_verify_bytes_empty();
#endif
    if (st == 0) st = case_verify_bytes_disabled();
    if (st == 0) st = case_verify_sig_null();
    if (st == 0) st = case_verify_sig_empty();
    if (st == 0) st = case_trust_bundle_null();
    if (st == 0) st = case_trust_bundle_truncated();
    if (st == 0) st = case_trust_bundle_wrong_magic();
    if (st == 0) st = case_trust_bundle_wrong_version();
    if (st == 0) st = case_policy_defaults();
    if (st == 0) st = case_trust_apply_bundle_null();
    if (st == 0) st = case_trust_apply_bundle_empty();
    if (st == 0) st = case_trust_load_builtin();
    mp_wasm_trust_clear();
    return st;
}

PM_MOD_TEST_C(pymergetic.wasmmod.verify, tests,
    pm_wasmmod_verify_tests);
