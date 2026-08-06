/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_wasmmod/inspect/source.h"
#include "source.h"

#include <stddef.h>

_Static_assert(sizeof(pm_wasmmod_source_info_t) == sizeof(mp_wasm_source_info_t), "source info layout");
_Static_assert(sizeof(pm_wasmmod_source_file_t) == sizeof(mp_wasm_source_file_t), "source file layout");
_Static_assert(sizeof(pm_wasmmod_source_tag_t) == sizeof(mp_wasm_source_tag_t), "source tag layout");

pm_wasmmod_source_t *pm_wasmmod_source_open_buffer(const uint8_t *wasm, uint32_t len) {
    return (pm_wasmmod_source_t *)mp_wasm_source_open_buffer(wasm, len);
}

pm_wasmmod_source_t *pm_wasmmod_source_open_owned(uint8_t *wasm, uint32_t len) {
    return (pm_wasmmod_source_t *)mp_wasm_source_open_owned(wasm, len);
}

pm_wasmmod_source_t *pm_wasmmod_source_open_file(const char *path) {
    return (pm_wasmmod_source_t *)mp_wasm_source_open_file(path);
}

pm_wasmmod_source_t *pm_wasmmod_source_open_name(const char *pack_name) {
    return (pm_wasmmod_source_t *)mp_wasm_source_open_name(pack_name);
}

void pm_wasmmod_source_close(pm_wasmmod_source_t *src) {
    mp_wasm_source_close((mp_wasm_source_view_t *)src);
}

const pm_wasmmod_source_info_t *pm_wasmmod_source_info(const pm_wasmmod_source_t *src) {
    return (const pm_wasmmod_source_info_t *)mp_wasm_source_info((const mp_wasm_source_view_t *)src);
}

bool pm_wasmmod_source_read(const pm_wasmmod_source_t *src, const char *path,
    uint8_t **out, uint32_t *out_len) {
    return mp_wasm_source_read((const mp_wasm_source_view_t *)src, path, out, out_len);
}

size_t pm_wasmmod_source_mount_prefix(const pm_wasmmod_source_t *src, char *buf, size_t buf_len) {
    return mp_wasm_source_mount_prefix((const mp_wasm_source_view_t *)src, buf, buf_len);
}

int pm_wasmmod_source_list_files(const pm_wasmmod_source_t *src, const char *module_or_null,
    void *ctx, pm_wasmmod_source_path_cb cb) {
    return mp_wasm_source_list_files((const mp_wasm_source_view_t *)src, module_or_null,
        ctx, (mp_wasm_source_path_cb)cb);
}

int pm_wasmmod_source_list_modules(const pm_wasmmod_source_t *src,
    void *ctx, pm_wasmmod_source_name_cb cb) {
    return mp_wasm_source_list_modules((const mp_wasm_source_view_t *)src,
        ctx, (mp_wasm_source_name_cb)cb);
}

int pm_wasmmod_source_list_submodules(const pm_wasmmod_source_t *src, const char *parent,
    void *ctx, pm_wasmmod_source_name_cb cb) {
    return mp_wasm_source_list_submodules((const mp_wasm_source_view_t *)src, parent,
        ctx, (mp_wasm_source_name_cb)cb);
}
