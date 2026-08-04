/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 */
#ifndef MICROPY_INCLUDED_EXTMOD_WASMMOD_FORMAT_ELF_LOAD_H
#define MICROPY_INCLUDED_EXTMOD_WASMMOD_FORMAT_ELF_LOAD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct mp_wasm_elf_sym_t {
    const char *name; // points into owned strtab copy or image
    void *addr;
    uint8_t st_info;
} mp_wasm_elf_sym_t;

typedef struct mp_wasm_elf_image_t {
    uint8_t *base;       // executable mapping
    size_t size;
    uint8_t *file_copy;  // owned ELF bytes (for strtab / debug)
    uint32_t file_len;
    mp_wasm_elf_sym_t *syms;
    uint32_t n_syms;
} mp_wasm_elf_image_t;

// Resolve an undefined ELF symbol (SHN_UNDEF) to a callable address.
// Return NULL to leave it unresolved (load fails if a reloc needs it).
typedef void *(*mp_wasm_elf_sym_resolve_t)(const char *name, void *ctx);

// Load ELF64 ET_REL (x86_64) into an executable image. No dlopen.
// resolve may be NULL when the object has no external undefs.
bool mp_wasm_elf_image_load(const uint8_t *elf, uint32_t len,
    mp_wasm_elf_sym_resolve_t resolve, void *resolve_ctx,
    mp_wasm_elf_image_t **out, char *errbuf, size_t errbuf_len);
void mp_wasm_elf_image_free(mp_wasm_elf_image_t *img);

// Global STT_FUNC / STT_NOTYPE with defined value.
void *mp_wasm_elf_lookup(const mp_wasm_elf_image_t *img, const char *name);

typedef void (*mp_wasm_elf_export_cb)(const char *name, void *addr, void *ctx);
void mp_wasm_elf_foreach_func(const mp_wasm_elf_image_t *img, mp_wasm_elf_export_cb cb, void *ctx);

#endif
