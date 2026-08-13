/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include "pymergetic/wasmmod/pack/format/common/format.h"

#include "pymergetic/wasmmod/pack/format/aot/section.h"
#include "pymergetic/wasmmod/pack/format/elf/section.h"
#include "pymergetic/wasmmod/pack/format/wasm/section.h"

mp_wasm_artifact_kind_t mp_wasm_artifact_kind(const uint8_t *buf, uint32_t len) {
    if (buf == NULL || len < 4) {
        return MP_WASM_KIND_UNKNOWN;
    }
    if (buf[0] == 0x00 && buf[1] == 'a' && buf[2] == 's' && buf[3] == 'm') {
        return MP_WASM_KIND_WASM;
    }
    if (buf[0] == 0x00 && buf[1] == 'a' && buf[2] == 'o' && buf[3] == 't') {
        return MP_WASM_KIND_AOT;
    }
    if (buf[0] == 0x7f && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') {
        return MP_WASM_KIND_ELF;
    }
    return MP_WASM_KIND_UNKNOWN;
}

bool mp_wasm_format_find_section(const uint8_t *buf, uint32_t len, const char *name,
    const uint8_t **payload, uint32_t *payload_len) {
    switch (mp_wasm_artifact_kind(buf, len)) {
        case MP_WASM_KIND_WASM:
            return mp_wasm_wasm_find_section(buf, len, name, payload, payload_len);
        case MP_WASM_KIND_AOT:
            return mp_wasm_aot_find_section(buf, len, name, payload, payload_len);
        case MP_WASM_KIND_ELF:
            return mp_wasm_elf_find_section(buf, len, name, payload, payload_len);
        default:
            return false;
    }
}

bool mp_wasm_format_strip_section(const uint8_t *buf, uint32_t len, const char *name,
    uint8_t **out, uint32_t *out_len) {
    switch (mp_wasm_artifact_kind(buf, len)) {
        case MP_WASM_KIND_WASM:
            return mp_wasm_wasm_strip_section(buf, len, name, out, out_len);
        case MP_WASM_KIND_AOT:
            return mp_wasm_aot_strip_section(buf, len, name, out, out_len);
        case MP_WASM_KIND_ELF:
            return mp_wasm_elf_strip_section(buf, len, name, out, out_len);
        default:
            return false;
    }
}

#endif // MICROPY_PY_WASM
