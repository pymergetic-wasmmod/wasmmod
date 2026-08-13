/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_FORMAT_WASM_SECTION_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_FORMAT_WASM_SECTION_H

#include <stdint.h>
#include <stdbool.h>

bool mp_wasm_wasm_find_section(const uint8_t *buf, uint32_t len, const char *name,
    const uint8_t **payload, uint32_t *payload_len);
bool mp_wasm_wasm_strip_section(const uint8_t *buf, uint32_t len, const char *name,
    uint8_t **out, uint32_t *out_len);

#endif
