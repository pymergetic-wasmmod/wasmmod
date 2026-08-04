/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * Pack inspect: ELF .symtab (+ has_dwarf). Addr2line: enclosing FUNC for now.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include "extmod/wasmmod/inspect.h"

#include <string.h>

#include "extmod/wasmmod/format/common/format.h"
#include "extmod/wasmmod/format/elf/section.h"

#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define STT_FUNC 2
#define STT_OBJECT 1
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define SHN_UNDEF 0

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

_Static_assert(sizeof(Elf64_Sym) == 24, "Elf64_Sym");

static void copy_name(char *dst, size_t dst_len, const char *src) {
    if (dst_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool mp_wasm_inspect_has_dwarf(const uint8_t *buf, uint32_t len) {
    if (mp_wasm_artifact_kind(buf, len) != MP_WASM_KIND_ELF) {
        return false;
    }
    const uint8_t *p = NULL;
    uint32_t plen = 0;
    if (mp_wasm_elf_find_section(buf, len, ".debug_line", &p, &plen) && plen > 0) {
        return true;
    }
    if (mp_wasm_elf_find_section(buf, len, "debug_line", &p, &plen) && plen > 0) {
        return true;
    }
    return false;
}

bool mp_wasm_inspect_symbols(const uint8_t *buf, uint32_t len,
    mp_wasm_sym_t *out, size_t cap, size_t *n_out) {
    if (n_out) {
        *n_out = 0;
    }
    if (buf == NULL || out == NULL || cap == 0) {
        return false;
    }
    if (mp_wasm_artifact_kind(buf, len) != MP_WASM_KIND_ELF) {
        return true; // empty ok for non-ELF
    }
    // Walk section headers via find — need full shdr walk; use format/elf internals lightly.
    // Minimal: parse ehdr/shdrs inline (same as section.c).
    if (len < 64 || buf[0] != 0x7f || buf[4] != 2 || buf[5] != 1) {
        return false;
    }
    uint64_t e_shoff;
    uint16_t e_shentsize, e_shnum, e_shstrndx;
    memcpy(&e_shoff, buf + 40, 8);
    memcpy(&e_shentsize, buf + 58, 2);
    memcpy(&e_shnum, buf + 60, 2);
    memcpy(&e_shstrndx, buf + 62, 2);
    if (e_shentsize < 64 || e_shnum == 0 || e_shstrndx >= e_shnum) {
        return false;
    }
    if (e_shoff + (uint64_t)e_shnum * e_shentsize > len) {
        return false;
    }
    const uint8_t *shstr = buf + e_shoff + (uint64_t)e_shstrndx * e_shentsize;
    uint64_t str_off, str_sz;
    memcpy(&str_off, shstr + 24, 8);
    memcpy(&str_sz, shstr + 32, 8);
    if (str_off + str_sz > len) {
        return false;
    }
    const uint8_t *strtab_hdr = NULL;
    uint64_t sym_off = 0, sym_sz = 0, sym_entsize = 0;
    uint32_t sym_link = 0;
    for (uint16_t i = 0; i < e_shnum; ++i) {
        const uint8_t *sh = buf + e_shoff + (uint64_t)i * e_shentsize;
        uint32_t sh_type;
        memcpy(&sh_type, sh + 4, 4);
        if (sh_type != SHT_SYMTAB) {
            continue;
        }
        memcpy(&sym_off, sh + 24, 8);
        memcpy(&sym_sz, sh + 32, 8);
        memcpy(&sym_link, sh + 40, 4);
        memcpy(&sym_entsize, sh + 56, 8);
        if (sym_link < e_shnum) {
            strtab_hdr = buf + e_shoff + (uint64_t)sym_link * e_shentsize;
        }
        break;
    }
    if (sym_entsize < sizeof(Elf64_Sym) || strtab_hdr == NULL) {
        return true;
    }
    uint64_t stab_off, stab_sz;
    memcpy(&stab_off, strtab_hdr + 24, 8);
    memcpy(&stab_sz, strtab_hdr + 32, 8);
    if (stab_off + stab_sz > len || sym_off + sym_sz > len) {
        return false;
    }
    const uint8_t *stab = buf + stab_off;
    size_t n = 0;
    uint64_t nsym = sym_sz / sym_entsize;
    for (uint64_t k = 0; k < nsym && n < cap; ++k) {
        Elf64_Sym sym;
        memcpy(&sym, buf + sym_off + k * sym_entsize, sizeof(sym));
        if (sym.st_shndx == SHN_UNDEF || sym.st_name == 0 || sym.st_name >= stab_sz) {
            continue;
        }
        const char *nm = (const char *)(stab + sym.st_name);
        if (nm[0] == '\0' || nm[0] == '.') {
            continue;
        }
        mp_wasm_sym_t *o = &out[n++];
        memset(o, 0, sizeof(*o));
        copy_name(o->name, sizeof(o->name), nm);
        o->section_index = (int32_t)sym.st_shndx;
        o->offset = sym.st_value;
        o->size = sym.st_size;
        uint8_t t = sym.st_info & 0xf;
        o->kind = (t == STT_FUNC) ? 1 : (t == STT_OBJECT) ? 2 : 0;
        uint8_t b = sym.st_info >> 4;
        o->binding = (b == STB_LOCAL) ? 0 : (b == STB_GLOBAL) ? 1 : (b == STB_WEAK) ? 2 : 0;
    }
    if (n_out) {
        *n_out = n;
    }
    return true;
}

bool mp_wasm_inspect_addr2line(const uint8_t *buf, uint32_t len, uint64_t addr,
    mp_wasm_loc_t *out, size_t cap, size_t *n_out) {
    if (n_out) {
        *n_out = 0;
    }
    if (out == NULL || cap == 0) {
        return false;
    }
    mp_wasm_sym_t syms[64];
    size_t n = 0;
    if (!mp_wasm_inspect_symbols(buf, len, syms, 64, &n)) {
        return false;
    }
    const mp_wasm_sym_t *best = NULL;
    for (size_t i = 0; i < n; ++i) {
        if (syms[i].kind != 1 || syms[i].size == 0) {
            continue;
        }
        if (addr >= syms[i].offset && addr < syms[i].offset + syms[i].size) {
            if (best == NULL || syms[i].offset >= best->offset) {
                best = &syms[i];
            }
        }
    }
    if (best == NULL) {
        return true;
    }
    memset(&out[0], 0, sizeof(out[0]));
    copy_name(out[0].path, sizeof(out[0].path), best->name);
    out[0].line = -1;
    out[0].role = 0; // sym
    if (n_out) {
        *n_out = 1;
    }
    return true;
}

#endif // MICROPY_PY_WASM
