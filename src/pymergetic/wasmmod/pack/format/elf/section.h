/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_FORMAT_ELF_SECTION_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_FORMAT_ELF_SECTION_H

#include <stdint.h>
#include <stdbool.h>

bool mp_wasm_elf_find_section(const uint8_t *buf, uint32_t len, const char *name,
    const uint8_t **payload, uint32_t *payload_len);
bool mp_wasm_elf_strip_section(const uint8_t *buf, uint32_t len, const char *name,
    uint8_t **out, uint32_t *out_len);
// Append SHT_PROGBITS named ".{name}" (or name if it already starts with '.').
bool mp_wasm_elf_append_section(const uint8_t *buf, uint32_t len, const char *name,
    const uint8_t *payload, uint32_t payload_len, uint8_t **out, uint32_t *out_len);

#endif
