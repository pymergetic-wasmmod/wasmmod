#include "ports/common/load.h"

#include <stdio.h>
#include <string.h>

#include "pymergetic/wasmmod/loader/__exports__.h"
#include "pymergetic/wasmmod/pack/zlib_env.h"
#include "pymergetic/wasmmod/verify/__exports__.h"

int pm_wasmmod_host_prepare(const uint8_t *in, uint32_t in_len, const char *path_hint,
    pm_wasmmod_host_prepared_t *out, char *err, size_t err_len) {
    if (out == NULL || in == NULL) {
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "null prepare args");
        }
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->bytes = in;
    out->len = in_len;
    out->owned = NULL;
    if (!mp_wasm_artifact_unwrap_zlib(&out->bytes, &out->len, &out->owned)) {
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "corrupt MPZL artifact");
        }
        return -1;
    }
    if (!mp_wasm_verify_bytes(out->bytes, out->len, path_hint, err, err_len)) {
        return -1;
    }
    out->kind = mp_wasm_artifact_kind(out->bytes, out->len);
    return 0;
}

pm_wasmmod_registry_handle_t pm_wasmmod_host_load_wasm(const char *fqn,
    const uint8_t *bytes, uint32_t len) {
    pm_wasmmod_registry_handle_t bad = { .index = UINT32_MAX, .generation = 0 };
    if (fqn == NULL || bytes == NULL) {
        return bad;
    }
    return pm_wasmmod_loader_load((const uint8_t *)fqn, (uint32_t)strlen(fqn), bytes, len);
}
