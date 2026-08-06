/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Embedded `wasmmod.source` custom section (WasmSource). Same face as
 * Python wasm.source / source_from_file / source_from_bytes.
 */

#ifndef PM_PM_WASMMOD_INSPECT_SOURCE_H_
#define PM_PM_WASMMOD_INSPECT_SOURCE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct pm_wasmmod_source pm_wasmmod_source_t;

typedef struct pm_wasmmod_source_file {
    const char *path;
    uint16_t path_len;
    uint8_t flags;
    uint32_t raw_len;
    const uint8_t *data;
    uint32_t data_len;
} pm_wasmmod_source_file_t;

typedef struct pm_wasmmod_source_tag {
    const char *key;
    uint16_t key_len;
    const char *value;
    uint16_t value_len;
} pm_wasmmod_source_tag_t;

typedef struct pm_wasmmod_source_info {
    uint16_t version;
    uint16_t flags;
    const char *name;
    uint16_t name_len;
    const char *pkg_version;
    uint16_t pkg_version_len;
    const pm_wasmmod_source_tag_t *tags;
    uint16_t n_tags;
    const pm_wasmmod_source_file_t *files;
    uint32_t n_files;
} pm_wasmmod_source_info_t;

pm_wasmmod_source_t *pm_wasmmod_source_open_buffer(const uint8_t *wasm, uint32_t len);
/** Takes ownership of wasm (freed on close). */
pm_wasmmod_source_t *pm_wasmmod_source_open_owned(uint8_t *wasm, uint32_t len);
pm_wasmmod_source_t *pm_wasmmod_source_open_file(const char *path);
pm_wasmmod_source_t *pm_wasmmod_source_open_name(const char *pack_name);
void pm_wasmmod_source_close(pm_wasmmod_source_t *src);

const pm_wasmmod_source_info_t *pm_wasmmod_source_info(const pm_wasmmod_source_t *src);

/** Read file (decompress if needed). Caller frees *out with the host wasm allocator. */
bool pm_wasmmod_source_read(const pm_wasmmod_source_t *src, const char *path,
    uint8_t **out, uint32_t *out_len);

size_t pm_wasmmod_source_mount_prefix(const pm_wasmmod_source_t *src, char *buf, size_t buf_len);

typedef int (*pm_wasmmod_source_path_cb)(void *ctx, const char *path, size_t path_len);
typedef int (*pm_wasmmod_source_name_cb)(void *ctx, const char *name, size_t name_len);

int pm_wasmmod_source_list_files(const pm_wasmmod_source_t *src, const char *module_or_null,
    void *ctx, pm_wasmmod_source_path_cb cb);
int pm_wasmmod_source_list_modules(const pm_wasmmod_source_t *src,
    void *ctx, pm_wasmmod_source_name_cb cb);
int pm_wasmmod_source_list_submodules(const pm_wasmmod_source_t *src, const char *parent,
    void *ctx, pm_wasmmod_source_name_cb cb);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_INSPECT_SOURCE_H_ */
