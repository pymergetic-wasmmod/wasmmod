/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#include "pm_wasmmod/host/self.h"

#include "alloc.h"

#include <string.h>

#if defined(__linux__) && !defined(__EMSCRIPTEN__)
#include <unistd.h>
#endif

static const uint8_t *s_image;
static uint32_t s_image_len;
static bool s_image_owned;

const char *pm_wasmmod_host_package_name(void) {
    return "pymergetic.wasmmod";
}

void pm_wasmmod_host_set_self_image(const uint8_t *buf, uint32_t len, bool take_ownership) {
    if (s_image_owned && s_image != NULL) {
        MICROPY_WASM_FREE((void *)s_image);
    }
    s_image = NULL;
    s_image_len = 0;
    s_image_owned = false;
    if (buf == NULL || len == 0) {
        return;
    }
    s_image = buf;
    s_image_len = len;
    s_image_owned = take_ownership;
}

const uint8_t *pm_wasmmod_host_self_image_ptr(uint32_t *len_out) {
    if (len_out != NULL) {
        *len_out = s_image_len;
    }
    return s_image;
}

pm_status_t pm_wasmmod_host_self_path(char *buf, size_t buflen) {
    if (buf == NULL || buflen < 2) {
        return PM_ERR_ARG;
    }
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
    ssize_t n = readlink("/proc/self/exe", buf, buflen - 1);
    if (n > 0) {
        buf[n] = '\0';
        return PM_OK;
    }
#endif
    (void)buf;
    (void)buflen;
    return PM_ERR_NOT_READY;
}

static pm_wasmmod_source_t *try_open_file(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return NULL;
    }
    return pm_wasmmod_source_open_file(path);
}

pm_wasmmod_source_t *pm_wasmmod_host_self_open(void) {
    if (s_image != NULL && s_image_len > 0) {
        return pm_wasmmod_source_open_buffer(s_image, s_image_len);
    }

    char path[4096];
    if (pm_wasmmod_host_self_path(path, sizeof(path)) == PM_OK) {
        pm_wasmmod_source_t *src = try_open_file(path);
        if (src != NULL) {
            return src;
        }
    }

    static const char *const cands[] = {
        "micropython.wasm",
        "pymergetic.wasmmod.wasm",
        "pymergetic.wasmmod.elf",
        NULL,
    };
    for (const char *const *p = cands; *p != NULL; ++p) {
        pm_wasmmod_source_t *src = try_open_file(*p);
        if (src != NULL) {
            return src;
        }
    }
    return NULL;
}
