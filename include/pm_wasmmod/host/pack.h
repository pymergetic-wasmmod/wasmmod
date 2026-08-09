/*
 * Host engine pack file face — paths under /mods/<pack.name>/ (one-module law).
 */
#ifndef PM_WASMMOD_HOST_PACK_H_
#define PM_WASMMOD_HOST_PACK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct pm_wasmmod_host_pack pm_wasmmod_host_pack_t;

/** Canonical VFS mount for the engine: `/mods/pymergetic.wasmmod`. */
const char *pm_wasmmod_host_pack_root(void);

/**
 * Open wasmmod.pack for the running host image (same resolve as host_self).
 * NULL if image/pack unavailable. Close with pm_wasmmod_host_pack_close.
 */
pm_wasmmod_host_pack_t *pm_wasmmod_host_self_pack_open(void);

void pm_wasmmod_host_pack_close(pm_wasmmod_host_pack_t *pack);

/** Package name from MPWP (borrowed; valid until close). */
const char *pm_wasmmod_host_pack_name(const pm_wasmmod_host_pack_t *pack);

/**
 * Read a pack file. `path` may be pack-relative or absolute under pack_root()
 * (e.g. `/mods/pymergetic.wasmmod/www/inspect/index.html`).
 * On success *out is MICROPY_WASM_MALLOC'd; caller frees.
 */
bool pm_wasmmod_host_pack_read(const pm_wasmmod_host_pack_t *pack, const char *path,
                               uint8_t **out, uint32_t *out_len);

typedef int (*pm_wasmmod_host_pack_path_cb)(void *ctx, const char *vfs_path, size_t path_len);

/** List files as absolute VFS paths under pack_root(). */
int pm_wasmmod_host_pack_list(const pm_wasmmod_host_pack_t *pack, void *ctx,
                              pm_wasmmod_host_pack_path_cb cb);

#ifdef __cplusplus
}
#endif

#endif
