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

#include <string.h>

#include "extmod/wasmmod/pack.h"

#include "extmod/wasmmod/alloc.h"
#include "extmod/wasmmod/zlibutil.h"

bool mp_wasm_read_uleb(const uint8_t **p, const uint8_t *end, uint32_t *out) {
    uint32_t result = 0;
    uint32_t shift = 0;
    while (*p < end) {
        uint8_t b = *(*p)++;
        result |= (uint32_t)(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            *out = result;
            return true;
        }
        shift += 7;
        if (shift > 28) {
            return false;
        }
    }
    return false;
}

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool mp_wasm_find_section_id(const uint8_t *wasm, uint32_t len, uint8_t want_id, const uint8_t **payload, uint32_t *payload_len) {
    if (wasm == NULL || len < 8 || wasm[0] != 0x00 || wasm[1] != 'a' || wasm[2] != 's' || wasm[3] != 'm') {
        return false;
    }
    const uint8_t *p = wasm + 8;
    const uint8_t *end = wasm + len;
    while (p < end) {
        uint8_t id = *p++;
        uint32_t size;
        if (!mp_wasm_read_uleb(&p, end, &size) || p + size > end) {
            return false;
        }
        if (id == want_id) {
            *payload = p;
            *payload_len = size;
            return true;
        }
        p += size;
    }
    return false;
}

bool mp_wasm_find_custom_section(const uint8_t *buf, uint32_t len, const char *name, const uint8_t **payload, uint32_t *payload_len) {
    if (buf == NULL || len < 8 || name == NULL) {
        return false;
    }
    // Wasm custom section (id 0).
    if (buf[0] == 0x00 && buf[1] == 'a' && buf[2] == 's' && buf[3] == 'm') {
        const uint8_t *p = buf + 8;
        const uint8_t *end = buf + len;
        const size_t want_len = strlen(name);

        while (p < end) {
            uint8_t id = *p++;
            uint32_t size;
            if (!mp_wasm_read_uleb(&p, end, &size) || p + size > end) {
                return false;
            }
            const uint8_t *sec = p;
            p += size;
            if (id != 0) {
                continue;
            }
            const uint8_t *q = sec;
            uint32_t name_len;
            if (!mp_wasm_read_uleb(&q, sec + size, &name_len) || q + name_len > sec + size) {
                continue;
            }
            if (name_len == want_len && memcmp(q, name, want_len) == 0) {
                q += name_len;
                *payload = q;
                *payload_len = (uint32_t)((sec + size) - q);
                return true;
            }
        }
        return false;
    }

    // WAMR AOT: section type 100 (CUSTOM), sub-type 0 (RAW), u16 name incl. NUL.
    if (buf[0] == 0x00 && buf[1] == 'a' && buf[2] == 'o' && buf[3] == 't') {
        const uint8_t *end = buf + len;
        const size_t want_len = strlen(name);
        uintptr_t p = 8;
        while (p + 8 <= len) {
            uint32_t typ = read_u32_le(buf + p);
            uint32_t size = read_u32_le(buf + p + 4);
            const uint8_t *content = buf + p + 8;
            if (content + size > end || size > 0x10000000u) {
                return false;
            }
            if (typ == 100 && size >= 6) {
                uint32_t sub = read_u32_le(content);
                if (sub == 0) {
                    uint16_t slen = read_u16_le(content + 4);
                    const uint8_t *nb = content + 6;
                    if (nb + slen <= content + size) {
                        size_t bare = slen;
                        if (bare > 0 && nb[bare - 1] == 0) {
                            bare--;
                        }
                        if (bare == want_len && memcmp(nb, name, want_len) == 0) {
                            *payload = nb + slen;
                            *payload_len = (uint32_t)((content + size) - (nb + slen));
                            return true;
                        }
                    }
                }
            }
            // Next header is 4-aligned (WAMR read_uint32 align_ptr).
            p = ((uintptr_t)(content + size - buf) + 3u) & ~(uintptr_t)3u;
        }
        return false;
    }

    return false;
}

bool mp_wasm_pack_find_section(const uint8_t *wasm, uint32_t len, const uint8_t **payload, uint32_t *payload_len) {
    return mp_wasm_find_custom_section(wasm, len, MP_WASM_PACK_SECTION, payload, payload_len);
}

bool mp_wasm_imports_find_section(const uint8_t *wasm, uint32_t len, const uint8_t **payload, uint32_t *payload_len) {
    return mp_wasm_find_custom_section(wasm, len, MP_WASM_IMPORTS_SECTION, payload, payload_len);
}

bool mp_wasm_pack_parse(const uint8_t *payload, uint32_t payload_len, mp_wasm_pack_info_t *out) {
    memset(out, 0, sizeof(*out));
    if (payload_len < 14 || memcmp(payload, MP_WASM_PACK_MAGIC, 4) != 0) {
        return false;
    }
    out->version = read_u16_le(payload + 4);
    out->flags = read_u16_le(payload + 6);
    if (out->version < 1 || out->version > 3) {
        return false;
    }
    uint16_t name_len = read_u16_le(payload + 8);
    if (10u + name_len + 4u > payload_len) {
        return false;
    }
    out->name = (const char *)(payload + 10);
    out->name_len = name_len;
    const uint8_t *p = payload + 10 + name_len;
    uint32_t n_files = read_u32_le(p);
    p += 4;
    if (n_files > 1024) {
        return false;
    }
    mp_wasm_pack_file_t *files = NULL;
    if (n_files > 0) {
        files = MICROPY_WASM_MALLOC(n_files * sizeof(mp_wasm_pack_file_t));
        if (files == NULL) {
            return false;
        }
    }
    const bool v3 = out->version >= 3;
    for (uint32_t i = 0; i < n_files; ++i) {
        if ((size_t)(p - payload) + 2 > payload_len) {
            MICROPY_WASM_FREE(files);
            return false;
        }
        uint16_t path_len = read_u16_le(p);
        p += 2;
        // v1/v2: path + kind + data_len; v3: + flags + raw_len + data_len
        size_t hdr = path_len + 1u + 4u + (v3 ? (1u + 4u) : 0u);
        if ((size_t)(p - payload) + hdr > payload_len) {
            MICROPY_WASM_FREE(files);
            return false;
        }
        files[i].path = (const char *)p;
        files[i].path_len = path_len;
        p += path_len;
        files[i].kind = *p++;
        if (v3) {
            files[i].flags = *p++;
            files[i].raw_len = read_u32_le(p);
            p += 4;
        } else {
            files[i].flags = 0;
            files[i].raw_len = 0; // filled after data_len
        }
        uint32_t data_len = read_u32_le(p);
        p += 4;
        if ((size_t)(p - payload) + data_len > payload_len) {
            MICROPY_WASM_FREE(files);
            return false;
        }
        files[i].data = p;
        files[i].data_len = data_len;
        if (!v3) {
            files[i].raw_len = data_len;
        }
        p += data_len;
    }
    out->files = files;
    out->n_files = n_files;

    if (out->version >= 2) {
        if ((size_t)(p - payload) + 4 > payload_len) {
            MICROPY_WASM_FREE(files);
            memset(out, 0, sizeof(*out));
            return false;
        }
        uint32_t n_exports = read_u32_le(p);
        p += 4;
        if (n_exports > 1024) {
            MICROPY_WASM_FREE(files);
            memset(out, 0, sizeof(*out));
            return false;
        }
        mp_wasm_pack_export_t *exports = NULL;
        if (n_exports > 0) {
            exports = MICROPY_WASM_MALLOC(n_exports * sizeof(mp_wasm_pack_export_t));
            if (exports == NULL) {
                MICROPY_WASM_FREE(files);
                memset(out, 0, sizeof(*out));
                return false;
            }
        }
        for (uint32_t i = 0; i < n_exports; ++i) {
            if ((size_t)(p - payload) + 2 > payload_len) {
                goto export_fail;
            }
            uint16_t module_len = read_u16_le(p);
            p += 2;
            if ((size_t)(p - payload) + module_len + 2 > payload_len) {
                goto export_fail;
            }
            exports[i].module = (const char *)p;
            exports[i].module_len = module_len;
            p += module_len;
            uint16_t func_len = read_u16_le(p);
            p += 2;
            if ((size_t)(p - payload) + func_len + 2 > payload_len) {
                goto export_fail;
            }
            exports[i].func = (const char *)p;
            exports[i].func_len = func_len;
            p += func_len;
            uint16_t export_len = read_u16_le(p);
            p += 2;
            if ((size_t)(p - payload) + export_len + 1 > payload_len) {
                goto export_fail;
            }
            exports[i].export_name = (const char *)p;
            exports[i].export_len = export_len;
            p += export_len;
            exports[i].sig = *p++;
        }
        out->exports = exports;
        out->n_exports = n_exports;
        return true;
    export_fail:
        MICROPY_WASM_FREE(exports);
        MICROPY_WASM_FREE(files);
        memset(out, 0, sizeof(*out));
        return false;
    }

    return true;
}

bool mp_wasm_pack_file_bytes(const mp_wasm_pack_file_t *f, const uint8_t **out, uint32_t *out_len, uint8_t **to_free) {
    if (to_free != NULL) {
        *to_free = NULL;
    }
    if (f == NULL || out == NULL || out_len == NULL) {
        return false;
    }
    if ((f->flags & MP_WASM_PACK_FILE_FLAG_ZLIB) == 0) {
        *out = f->data;
        *out_len = f->data_len;
        return true;
    }
    if (f->raw_len == 0 || f->data_len == 0) {
        return false;
    }
    uint8_t *raw = MICROPY_WASM_MALLOC(f->raw_len);
    if (raw == NULL) {
        return false;
    }
    if (!mp_wasm_zlib_inflate(f->data, f->data_len, raw, f->raw_len)) {
        MICROPY_WASM_FREE(raw);
        return false;
    }
    *out = raw;
    *out_len = f->raw_len;
    if (to_free != NULL) {
        *to_free = raw;
    }
    return true;
}

void mp_wasm_pack_info_free(mp_wasm_pack_info_t *info) {
    if (info == NULL) {
        return;
    }
    MICROPY_WASM_FREE((void *)info->files);
    MICROPY_WASM_FREE((void *)info->exports);
    info->files = NULL;
    info->exports = NULL;
    info->n_files = 0;
    info->n_exports = 0;
}

bool mp_wasm_imports_parse(const uint8_t *payload, uint32_t payload_len, mp_wasm_imports_info_t *out) {
    memset(out, 0, sizeof(*out));
    if (payload_len < 10 || memcmp(payload, MP_WASM_IMPORTS_MAGIC, 4) != 0) {
        return false;
    }
    out->version = read_u16_le(payload + 4);
    if (out->version != 1) {
        return false;
    }
    uint32_t n = read_u32_le(payload + 6);
    if (n > 1024) {
        return false;
    }
    const uint8_t *p = payload + 10;
    mp_wasm_import_t *imports = NULL;
    if (n > 0) {
        imports = MICROPY_WASM_MALLOC(n * sizeof(mp_wasm_import_t));
        if (imports == NULL) {
            return false;
        }
    }
    for (uint32_t i = 0; i < n; ++i) {
        if ((size_t)(p - payload) + 2 > payload_len) {
            MICROPY_WASM_FREE(imports);
            return false;
        }
        uint16_t module_len = read_u16_le(p);
        p += 2;
        if ((size_t)(p - payload) + module_len + 2 > payload_len) {
            MICROPY_WASM_FREE(imports);
            return false;
        }
        imports[i].module = (const char *)p;
        imports[i].module_len = module_len;
        p += module_len;
        uint16_t func_len = read_u16_le(p);
        p += 2;
        if ((size_t)(p - payload) + func_len > payload_len) {
            MICROPY_WASM_FREE(imports);
            return false;
        }
        imports[i].func = (const char *)p;
        imports[i].func_len = func_len;
        p += func_len;
    }
    out->imports = imports;
    out->n_imports = n;
    return true;
}

void mp_wasm_imports_info_free(mp_wasm_imports_info_t *info) {
    if (info == NULL) {
        return;
    }
    MICROPY_WASM_FREE((void *)info->imports);
    info->imports = NULL;
    info->n_imports = 0;
}

bool mp_wasm_deps_find_section(const uint8_t *wasm, uint32_t len, const uint8_t **payload, uint32_t *payload_len) {
    return mp_wasm_find_custom_section(wasm, len, MP_WASM_DEPS_SECTION, payload, payload_len);
}

bool mp_wasm_deps_parse(const uint8_t *payload, uint32_t payload_len, mp_wasm_deps_info_t *out) {
    memset(out, 0, sizeof(*out));
    if (payload_len < 10 || memcmp(payload, MP_WASM_DEPS_MAGIC, 4) != 0) {
        return false;
    }
    out->version = read_u16_le(payload + 4);
    if (out->version != 1) {
        return false;
    }
    uint32_t n = read_u32_le(payload + 6);
    if (n > 1024) {
        return false;
    }
    const uint8_t *p = payload + 10;
    mp_wasm_dep_t *deps = NULL;
    if (n > 0) {
        deps = MICROPY_WASM_MALLOC(n * sizeof(mp_wasm_dep_t));
        if (deps == NULL) {
            return false;
        }
    }
    for (uint32_t i = 0; i < n; ++i) {
        if ((size_t)(p - payload) + 2 > payload_len) {
            MICROPY_WASM_FREE(deps);
            return false;
        }
        uint16_t name_len = read_u16_le(p);
        p += 2;
        if ((size_t)(p - payload) + name_len + 2 > payload_len) {
            MICROPY_WASM_FREE(deps);
            return false;
        }
        deps[i].name = (const char *)p;
        deps[i].name_len = name_len;
        p += name_len;
        uint16_t ver_len = read_u16_le(p);
        p += 2;
        if ((size_t)(p - payload) + ver_len > payload_len) {
            MICROPY_WASM_FREE(deps);
            return false;
        }
        deps[i].version = (const char *)p;
        deps[i].version_len = ver_len;
        p += ver_len;
    }
    out->deps = deps;
    out->n_deps = n;
    return true;
}

void mp_wasm_deps_info_free(mp_wasm_deps_info_t *info) {
    if (info == NULL) {
        return;
    }
    MICROPY_WASM_FREE((void *)info->deps);
    info->deps = NULL;
    info->n_deps = 0;
}

#endif // MICROPY_PY_WASM
