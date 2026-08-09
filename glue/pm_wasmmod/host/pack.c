/*
 * Host pack VFS face — MPWP from running image, paths /mods/<name>/….
 */
#include "pm_wasmmod/host/pack.h"
#include "pm_wasmmod/host/self.h"

#include "alloc.h"
#include "../../../pack.h" /* mp_pack_manifest_* (not host/pack.h) */

#include <stdio.h>
#include <string.h>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <unistd.h>
#endif

struct pm_wasmmod_host_pack {
    uint8_t *owned_image;
    uint32_t image_len;
    const uint8_t *image;
    mp_pack_manifest_t man;
    bool have_man;
    char name_buf[96];
};

const char *pm_wasmmod_host_pack_root(void) {
    return "/mods/pymergetic.wasmmod";
}

static uint8_t *read_file_all(const char *path, uint32_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > 256 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = MICROPY_WASM_MALLOC((size_t)sz ? (size_t)sz : 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (n != (size_t)sz) {
        MICROPY_WASM_FREE(buf);
        return NULL;
    }
    *out_len = (uint32_t)n;
    return buf;
}

static pm_wasmmod_host_pack_t *pack_from_bytes(const uint8_t *img, uint32_t len, uint8_t *owned) {
    const uint8_t *payload = NULL;
    uint32_t plen = 0;
    if (!mp_pack_manifest_find_section(img, len, &payload, &plen)) {
        if (owned) {
            MICROPY_WASM_FREE(owned);
        }
        return NULL;
    }
    pm_wasmmod_host_pack_t *p = MICROPY_WASM_MALLOC(sizeof(*p));
    if (p == NULL) {
        if (owned) {
            MICROPY_WASM_FREE(owned);
        }
        return NULL;
    }
    memset(p, 0, sizeof(*p));
    p->image = img;
    p->image_len = len;
    p->owned_image = owned;
    if (!mp_pack_manifest_parse(payload, plen, &p->man)) {
        pm_wasmmod_host_pack_close(p);
        return NULL;
    }
    p->have_man = true;
    size_t nl = p->man.name_len < sizeof(p->name_buf) - 1 ? p->man.name_len : sizeof(p->name_buf) - 1;
    memcpy(p->name_buf, p->man.name, nl);
    p->name_buf[nl] = '\0';
    return p;
}

pm_wasmmod_host_pack_t *pm_wasmmod_host_self_pack_open(void) {
    /* Prefer pinned browser/self image — same as host_self_open. */
    uint32_t ilen = 0;
    const uint8_t *img = pm_wasmmod_host_self_image_ptr(&ilen);
    if (img != NULL && ilen > 0) {
        return pack_from_bytes(img, ilen, NULL);
    }

    char path[4096];
    if (pm_wasmmod_host_self_path(path, sizeof(path)) == PM_OK) {
        uint32_t n = 0;
        uint8_t *buf = read_file_all(path, &n);
        if (buf != NULL) {
            return pack_from_bytes(buf, n, buf);
        }
    }

    static const char *const cands[] = {
        "micropython.wasm",
        "pymergetic.wasmmod.wasm",
        "pymergetic.wasmmod.elf",
        NULL,
    };
    for (const char *const *c = cands; *c != NULL; ++c) {
        uint32_t n = 0;
        uint8_t *buf = read_file_all(*c, &n);
        if (buf != NULL) {
            pm_wasmmod_host_pack_t *p = pack_from_bytes(buf, n, buf);
            if (p != NULL) {
                return p;
            }
        }
    }
    return NULL;
}

void pm_wasmmod_host_pack_close(pm_wasmmod_host_pack_t *pack) {
    if (pack == NULL) {
        return;
    }
    if (pack->have_man) {
        mp_pack_manifest_free(&pack->man);
        pack->have_man = false;
    }
    if (pack->owned_image != NULL) {
        MICROPY_WASM_FREE(pack->owned_image);
        pack->owned_image = NULL;
    }
    MICROPY_WASM_FREE(pack);
}

const char *pm_wasmmod_host_pack_name(const pm_wasmmod_host_pack_t *pack) {
    if (pack == NULL || pack->name_buf[0] == '\0') {
        return pm_wasmmod_host_package_name();
    }
    return pack->name_buf;
}

static const char *strip_vfs_prefix(const pm_wasmmod_host_pack_t *pack, const char *path) {
    if (path == NULL) {
        return NULL;
    }
    while (*path == '/') {
        path++;
    }
    const char *root = "mods/";
    if (strncmp(path, root, 5) == 0) {
        path += 5;
        const char *name = pm_wasmmod_host_pack_name(pack);
        size_t nl = strlen(name);
        if (strncmp(path, name, nl) == 0 && (path[nl] == '/' || path[nl] == '\0')) {
            path += nl;
            if (*path == '/') {
                path++;
            }
            return path;
        }
        return NULL;
    }
    return path;
}

bool pm_wasmmod_host_pack_read(const pm_wasmmod_host_pack_t *pack, const char *path,
                               uint8_t **out, uint32_t *out_len) {
    if (pack == NULL || !pack->have_man || path == NULL || out == NULL || out_len == NULL) {
        return false;
    }
    const char *rel = strip_vfs_prefix(pack, path);
    if (rel == NULL) {
        return false;
    }
    size_t rlen = strlen(rel);
    for (uint32_t i = 0; i < pack->man.n_files; ++i) {
        const mp_pack_manifest_file_t *f = &pack->man.files[i];
        if (f->path_len == rlen && memcmp(f->path, rel, rlen) == 0) {
            const uint8_t *data = NULL;
            uint32_t dlen = 0;
            uint8_t *to_free = NULL;
            if (!mp_pack_manifest_file_bytes(f, &data, &dlen, &to_free)) {
                return false;
            }
            if (to_free != NULL) {
                *out = to_free;
            } else {
                uint8_t *copy = MICROPY_WASM_MALLOC(dlen ? dlen : 1);
                if (copy == NULL) {
                    return false;
                }
                if (dlen) {
                    memcpy(copy, data, dlen);
                }
                *out = copy;
            }
            *out_len = dlen;
            return true;
        }
    }
    return false;
}

int pm_wasmmod_host_pack_list(const pm_wasmmod_host_pack_t *pack, void *ctx,
                              pm_wasmmod_host_pack_path_cb cb) {
    if (pack == NULL || !pack->have_man || cb == NULL) {
        return -1;
    }
    char vfs[256];
    const char *name = pm_wasmmod_host_pack_name(pack);
    for (uint32_t i = 0; i < pack->man.n_files; ++i) {
        const mp_pack_manifest_file_t *f = &pack->man.files[i];
        int n = snprintf(vfs, sizeof(vfs), "/mods/%s/%.*s", name, (int)f->path_len, f->path);
        if (n <= 0 || (size_t)n >= sizeof(vfs)) {
            continue;
        }
        if (cb(ctx, vfs, (size_t)n) != 0) {
            return -1;
        }
    }
    return 0;
}
