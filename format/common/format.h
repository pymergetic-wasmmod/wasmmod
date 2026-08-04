/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_FORMAT_COMMON_FORMAT_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_FORMAT_COMMON_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    MP_WASM_KIND_UNKNOWN = 0,
    MP_WASM_KIND_WASM = 1,
    MP_WASM_KIND_AOT = 2,
    MP_WASM_KIND_ELF = 3,
} mp_wasm_artifact_kind_t;

mp_wasm_artifact_kind_t mp_wasm_artifact_kind(const uint8_t *buf, uint32_t len);

// Named metadata payload (wasmmod.pack / .imports / .deps / .sig / .source).
// ELF section names are ".wasmmod.*" (leading dot); logical name omits the dot.
bool mp_wasm_format_find_section(const uint8_t *buf, uint32_t len, const char *name,
    const uint8_t **payload, uint32_t *payload_len);

// Drop named section; *out is malloc'd (MICROPY_WASM_MALLOC).
bool mp_wasm_format_strip_section(const uint8_t *buf, uint32_t len, const char *name,
    uint8_t **out, uint32_t *out_len);

#endif // MICROPY_INCLUDED_EXTMOD_WASMMOD_FORMAT_COMMON_FORMAT_H
