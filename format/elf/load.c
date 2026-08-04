/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * In-tree ELF64 ET_REL loader (x86_64). No dlopen / ld.so.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#if MICROPY_PY_WASM && MICROPY_PY_WASM_ELF

#include "extmod/wasmmod/format/elf/load.h"

#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "extmod/wasmmod/alloc.h"

#define EI_NIDENT 16
#define ET_REL 1
#define EM_X86_64 62
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define ELF64_ST_TYPE(i) ((i) & 0xf)
#define ELF64_ST_BIND(i) ((i) >> 4)
#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xffffffffu)

#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_GOT32 3
#define R_X86_64_PLT32 4
#define R_X86_64_32 10
#define R_X86_64_32S 11
#define R_X86_64_PC64 24
#define R_X86_64_GOTPCREL 9
#define R_X86_64_REX_GOTPCRELX 42
#define R_X86_64_GOTPCRELX 41

#pragma pack(push, 1)
typedef struct {
    uint8_t e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Elf64_Rela;
#pragma pack(pop)

static void set_err(char *errbuf, size_t errbuf_len, const char *msg) {
    if (errbuf == NULL || errbuf_len == 0) {
        return;
    }
    if (msg == NULL) {
        msg = "elf load error";
    }
    size_t n = strlen(msg);
    if (n >= errbuf_len) {
        n = errbuf_len - 1;
    }
    memcpy(errbuf, msg, n);
    errbuf[n] = '\0';
}

static bool parse_ehdr(const uint8_t *buf, uint32_t len, Elf64_Ehdr *eh, char *errbuf, size_t errbuf_len) {
    if (buf == NULL || len < sizeof(Elf64_Ehdr)) {
        set_err(errbuf, errbuf_len, "elf truncated");
        return false;
    }
    if (buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') {
        set_err(errbuf, errbuf_len, "not ELF");
        return false;
    }
    if (buf[4] != ELFCLASS64 || buf[5] != ELFDATA2LSB) {
        set_err(errbuf, errbuf_len, "need ELF64 LE");
        return false;
    }
    memcpy(eh, buf, sizeof(*eh));
    if (eh->e_type != ET_REL) {
        set_err(errbuf, errbuf_len, "need ET_REL");
        return false;
    }
    if (eh->e_machine != EM_X86_64) {
        set_err(errbuf, errbuf_len, "need EM_X86_64");
        return false;
    }
    if (eh->e_shnum == 0 || eh->e_shentsize < sizeof(Elf64_Shdr)) {
        set_err(errbuf, errbuf_len, "bad section headers");
        return false;
    }
    uint64_t sh_end = eh->e_shoff + (uint64_t)eh->e_shnum * eh->e_shentsize;
    if (eh->e_shoff >= len || sh_end > len) {
        set_err(errbuf, errbuf_len, "shdr out of range");
        return false;
    }
    return true;
}

static bool get_shdr(const uint8_t *buf, uint32_t len, const Elf64_Ehdr *eh, uint16_t i, Elf64_Shdr *sh) {
    if (i >= eh->e_shnum) {
        return false;
    }
    uint64_t off = eh->e_shoff + (uint64_t)i * eh->e_shentsize;
    if (off + sizeof(Elf64_Shdr) > len) {
        return false;
    }
    memcpy(sh, buf + off, sizeof(*sh));
    return true;
}

static uintptr_t align_up(uintptr_t v, uintptr_t a) {
    if (a <= 1) {
        return v;
    }
    return (v + a - 1) & ~(a - 1);
}

void mp_wasm_elf_image_free(mp_wasm_elf_image_t *img) {
    if (img == NULL) {
        return;
    }
    if (img->base != NULL && img->size > 0) {
        munmap(img->base, img->size);
    }
    if (img->file_copy) {
        MICROPY_WASM_FREE(img->file_copy);
    }
    if (img->syms) {
        MICROPY_WASM_FREE(img->syms);
    }
    MICROPY_WASM_FREE(img);
}

bool mp_wasm_elf_image_load(const uint8_t *elf, uint32_t len,
    mp_wasm_elf_sym_resolve_t resolve, void *resolve_ctx,
    mp_wasm_elf_image_t **out, char *errbuf, size_t errbuf_len) {
    if (out == NULL) {
        return false;
    }
    *out = NULL;
    Elf64_Ehdr eh;
    if (!parse_ehdr(elf, len, &eh, errbuf, errbuf_len)) {
        return false;
    }

    // Layout ALLOC sections into a contiguous RWX image.
    uintptr_t *sec_addr = MICROPY_WASM_MALLOC((size_t)eh.e_shnum * sizeof(uintptr_t));
    if (sec_addr == NULL) {
        set_err(errbuf, errbuf_len, "oom");
        return false;
    }
    memset(sec_addr, 0, (size_t)eh.e_shnum * sizeof(uintptr_t));

    size_t image_size = 0;
    for (uint16_t i = 0; i < eh.e_shnum; ++i) {
        Elf64_Shdr sh;
        if (!get_shdr(elf, len, &eh, i, &sh)) {
            MICROPY_WASM_FREE(sec_addr);
            set_err(errbuf, errbuf_len, "bad shdr");
            return false;
        }
        if (!(sh.sh_flags & SHF_ALLOC) || sh.sh_size == 0) {
            continue;
        }
        uintptr_t align = sh.sh_addralign ? (uintptr_t)sh.sh_addralign : 1;
        image_size = align_up(image_size, align);
        sec_addr[i] = image_size;
        image_size += (size_t)sh.sh_size;
    }
    if (image_size == 0) {
        image_size = 4096;
    }

    // Find symtab early (needed for GOT sizing + resolve).
    int symtab_i = -1;
    for (uint16_t i = 0; i < eh.e_shnum; ++i) {
        Elf64_Shdr sh;
        get_shdr(elf, len, &eh, i, &sh);
        if (sh.sh_type == SHT_SYMTAB) {
            symtab_i = (int)i;
            break;
        }
    }
    if (symtab_i < 0) {
        MICROPY_WASM_FREE(sec_addr);
        set_err(errbuf, errbuf_len, "no symtab");
        return false;
    }
    Elf64_Shdr symsh, strsh;
    get_shdr(elf, len, &eh, (uint16_t)symtab_i, &symsh);
    if (!get_shdr(elf, len, &eh, (uint16_t)symsh.sh_link, &strsh)
        || strsh.sh_type != SHT_STRTAB
        || symsh.sh_offset + symsh.sh_size > len
        || strsh.sh_offset + strsh.sh_size > len
        || symsh.sh_entsize < sizeof(Elf64_Sym)) {
        MICROPY_WASM_FREE(sec_addr);
        set_err(errbuf, errbuf_len, "bad symtab");
        return false;
    }
    uint32_t nsym = (uint32_t)(symsh.sh_size / symsh.sh_entsize);
    const Elf64_Sym *syms = (const Elf64_Sym *)(elf + symsh.sh_offset);
    const char *strtab = (const char *)(elf + strsh.sh_offset);

    // GOT slots for GOTPCREL* (compilers emit these even with -fno-pic).
    uint32_t *got_off = MICROPY_WASM_MALLOC((size_t)nsym * sizeof(uint32_t));
    if (got_off == NULL) {
        MICROPY_WASM_FREE(sec_addr);
        set_err(errbuf, errbuf_len, "oom");
        return false;
    }
    memset(got_off, 0xff, (size_t)nsym * sizeof(uint32_t)); // ~0u = no slot
    uint32_t n_got = 0;
    for (uint16_t i = 0; i < eh.e_shnum; ++i) {
        Elf64_Shdr sh;
        get_shdr(elf, len, &eh, i, &sh);
        if (sh.sh_type != SHT_RELA || sh.sh_entsize < sizeof(Elf64_Rela)
            || sh.sh_offset + sh.sh_size > len) {
            continue;
        }
        uint32_t nrel = (uint32_t)(sh.sh_size / sh.sh_entsize);
        for (uint32_t r = 0; r < nrel; ++r) {
            const Elf64_Rela *rela = (const Elf64_Rela *)(elf + sh.sh_offset + (size_t)r * sh.sh_entsize);
            uint32_t typ = (uint32_t)ELF64_R_TYPE(rela->r_info);
            uint32_t sym_i = (uint32_t)ELF64_R_SYM(rela->r_info);
            if (sym_i >= nsym) {
                continue;
            }
            if (typ == R_X86_64_GOTPCREL || typ == R_X86_64_GOTPCRELX
                || typ == R_X86_64_REX_GOTPCRELX || typ == R_X86_64_GOT32) {
                if (got_off[sym_i] == ~0u) {
                    got_off[sym_i] = n_got++;
                }
            }
        }
    }
    size_t got_base = align_up(image_size, 8);
    size_t got_bytes = (size_t)n_got * 8;
    image_size = got_base + got_bytes;

    // Page-align mapping.
    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t map_size = align_up(image_size, page);

    void *map = mmap(NULL, map_size, PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map == MAP_FAILED) {
        MICROPY_WASM_FREE(sec_addr);
        MICROPY_WASM_FREE(got_off);
        set_err(errbuf, errbuf_len, "mmap failed");
        return false;
    }
    memset(map, 0, map_size);
    uint8_t *base = (uint8_t *)map;

    for (uint16_t i = 0; i < eh.e_shnum; ++i) {
        Elf64_Shdr sh;
        get_shdr(elf, len, &eh, i, &sh);
        if (!(sh.sh_flags & SHF_ALLOC) || sh.sh_size == 0) {
            continue;
        }
        if (sh.sh_type == SHT_NOBITS) {
            continue; // already zeroed
        }
        if (sh.sh_offset + sh.sh_size > len) {
            munmap(map, map_size);
            MICROPY_WASM_FREE(sec_addr);
            MICROPY_WASM_FREE(got_off);
            set_err(errbuf, errbuf_len, "section data OOB");
            return false;
        }
        memcpy(base + sec_addr[i], elf + sh.sh_offset, (size_t)sh.sh_size);
    }

    // Resolve symbol address: defined → image; undef → optional resolve callback.
    uintptr_t *sym_addr = MICROPY_WASM_MALLOC((size_t)nsym * sizeof(uintptr_t));
    if (sym_addr == NULL) {
        munmap(map, map_size);
        MICROPY_WASM_FREE(sec_addr);
        MICROPY_WASM_FREE(got_off);
        set_err(errbuf, errbuf_len, "oom");
        return false;
    }
    memset(sym_addr, 0, (size_t)nsym * sizeof(uintptr_t));
    for (uint32_t i = 0; i < nsym; ++i) {
        const Elf64_Sym *s = (const Elf64_Sym *)((const uint8_t *)syms + (size_t)i * symsh.sh_entsize);
        if (s->st_shndx == SHN_UNDEF) {
            if (s->st_name != 0 && s->st_name < strsh.sh_size) {
                const char *nm = strtab + s->st_name;
                if (strcmp(nm, "_GLOBAL_OFFSET_TABLE_") == 0) {
                    sym_addr[i] = (uintptr_t)(base + got_base);
                    continue;
                }
                if (resolve != NULL && nm[0] != '\0') {
                    void *p = resolve(nm, resolve_ctx);
                    if (p != NULL) {
                        sym_addr[i] = (uintptr_t)p;
                    }
                }
            }
            continue;
        }
        if (s->st_shndx == SHN_ABS) {
            sym_addr[i] = (uintptr_t)s->st_value;
            continue;
        }
        if (s->st_shndx == SHN_COMMON || s->st_shndx >= eh.e_shnum) {
            continue;
        }
        sym_addr[i] = (uintptr_t)(base + sec_addr[s->st_shndx] + s->st_value);
    }

    // Fill GOT entries with resolved symbol addresses.
    for (uint32_t i = 0; i < nsym; ++i) {
        if (got_off[i] == ~0u) {
            continue;
        }
        *(uint64_t *)(base + got_base + (size_t)got_off[i] * 8) = (uint64_t)sym_addr[i];
    }

    // Apply RELA
    for (uint16_t i = 0; i < eh.e_shnum; ++i) {
        Elf64_Shdr sh;
        get_shdr(elf, len, &eh, i, &sh);
        if (sh.sh_type != SHT_RELA) {
            continue;
        }
        if (sh.sh_info >= eh.e_shnum || sh.sh_entsize < sizeof(Elf64_Rela)
            || sh.sh_offset + sh.sh_size > len) {
            munmap(map, map_size);
            MICROPY_WASM_FREE(sec_addr);
            MICROPY_WASM_FREE(got_off);
            MICROPY_WASM_FREE(sym_addr);
            set_err(errbuf, errbuf_len, "bad rela");
            return false;
        }
        uint32_t nrel = (uint32_t)(sh.sh_size / sh.sh_entsize);
        uint8_t *target_base = base + sec_addr[sh.sh_info];
        for (uint32_t r = 0; r < nrel; ++r) {
            const Elf64_Rela *rela = (const Elf64_Rela *)(elf + sh.sh_offset + (size_t)r * sh.sh_entsize);
            uint32_t sym_i = (uint32_t)ELF64_R_SYM(rela->r_info);
            uint32_t typ = (uint32_t)ELF64_R_TYPE(rela->r_info);
            if (sym_i >= nsym) {
                munmap(map, map_size);
                MICROPY_WASM_FREE(sec_addr);
                MICROPY_WASM_FREE(got_off);
                MICROPY_WASM_FREE(sym_addr);
                set_err(errbuf, errbuf_len, "rela sym OOB");
                return false;
            }
            uintptr_t S = sym_addr[sym_i];
            if (S == 0 && typ != R_X86_64_NONE) {
                const Elf64_Sym *s = (const Elf64_Sym *)((const uint8_t *)syms + (size_t)sym_i * symsh.sh_entsize);
                const char *nm = (s->st_name < strsh.sh_size) ? (strtab + s->st_name) : "?";
                char msg[160];
                snprintf(msg, sizeof(msg), "unresolved symbol: %s", nm);
                munmap(map, map_size);
                MICROPY_WASM_FREE(sec_addr);
                MICROPY_WASM_FREE(got_off);
                MICROPY_WASM_FREE(sym_addr);
                set_err(errbuf, errbuf_len, msg);
                return false;
            }
            uint8_t *P = target_base + rela->r_offset;
            int64_t A = rela->r_addend;
            switch (typ) {
                case R_X86_64_NONE:
                    break;
                case R_X86_64_64:
                    *(uint64_t *)P = (uint64_t)(S + (uintptr_t)A);
                    break;
                case R_X86_64_PC32:
                case R_X86_64_PLT32: {
                    int64_t v = (int64_t)S + A - (int64_t)(uintptr_t)P;
                    *(int32_t *)P = (int32_t)v;
                    break;
                }
                case R_X86_64_GOTPCREL:
                case R_X86_64_GOTPCRELX:
                case R_X86_64_REX_GOTPCRELX: {
                    // G = address of GOT entry for S; reloc is G + A - P.
                    uintptr_t G = (uintptr_t)(base + got_base + (size_t)got_off[sym_i] * 8);
                    int64_t v = (int64_t)G + A - (int64_t)(uintptr_t)P;
                    *(int32_t *)P = (int32_t)v;
                    break;
                }
                case R_X86_64_GOT32: {
                    // Offset of GOT entry from GOT base (+ addend).
                    *(int32_t *)P = (int32_t)((int64_t)got_off[sym_i] * 8 + A);
                    break;
                }
                case R_X86_64_32:
                case R_X86_64_32S:
                    *(uint32_t *)P = (uint32_t)(S + (uintptr_t)A);
                    break;
                case R_X86_64_PC64: {
                    int64_t v = (int64_t)S + A - (int64_t)(uintptr_t)P;
                    *(int64_t *)P = v;
                    break;
                }
                default: {
                    char msg[80];
                    snprintf(msg, sizeof(msg), "unsupported reloc %u", (unsigned)typ);
                    munmap(map, map_size);
                    MICROPY_WASM_FREE(sec_addr);
                    MICROPY_WASM_FREE(got_off);
                    MICROPY_WASM_FREE(sym_addr);
                    set_err(errbuf, errbuf_len, msg);
                    return false;
                }
            }
        }
    }

    // Publish global function symbols
    uint32_t n_pub = 0;
    for (uint32_t i = 0; i < nsym; ++i) {
        const Elf64_Sym *s = (const Elf64_Sym *)((const uint8_t *)syms + (size_t)i * symsh.sh_entsize);
        uint8_t bind = ELF64_ST_BIND(s->st_info);
        uint8_t typ = ELF64_ST_TYPE(s->st_info);
        if (s->st_shndx == SHN_UNDEF || s->st_name == 0) {
            continue;
        }
        if (bind != STB_GLOBAL && bind != STB_WEAK) {
            continue;
        }
        if (typ != STT_FUNC && typ != STT_NOTYPE) {
            continue;
        }
        if (sym_addr[i] == 0) {
            continue;
        }
        n_pub++;
    }

    mp_wasm_elf_image_t *img = MICROPY_WASM_MALLOC(sizeof(*img));
    if (img == NULL) {
        munmap(map, map_size);
        MICROPY_WASM_FREE(sec_addr);
        MICROPY_WASM_FREE(got_off);
        MICROPY_WASM_FREE(sym_addr);
        set_err(errbuf, errbuf_len, "oom");
        return false;
    }
    memset(img, 0, sizeof(*img));
    img->base = base;
    img->size = map_size;
    img->file_copy = MICROPY_WASM_MALLOC(len);
    if (img->file_copy == NULL) {
        mp_wasm_elf_image_free(img);
        MICROPY_WASM_FREE(sec_addr);
        MICROPY_WASM_FREE(got_off);
        MICROPY_WASM_FREE(sym_addr);
        set_err(errbuf, errbuf_len, "oom");
        return false;
    }
    memcpy(img->file_copy, elf, len);
    img->file_len = len;
    img->n_syms = n_pub;
    if (n_pub > 0) {
        img->syms = MICROPY_WASM_MALLOC((size_t)n_pub * sizeof(mp_wasm_elf_sym_t));
        if (img->syms == NULL) {
            mp_wasm_elf_image_free(img);
            MICROPY_WASM_FREE(sec_addr);
            MICROPY_WASM_FREE(got_off);
            MICROPY_WASM_FREE(sym_addr);
            set_err(errbuf, errbuf_len, "oom");
            return false;
        }
        uint32_t w = 0;
        // strtab now in file_copy
        const char *str2 = (const char *)(img->file_copy + strsh.sh_offset);
        for (uint32_t i = 0; i < nsym; ++i) {
            const Elf64_Sym *s = (const Elf64_Sym *)((const uint8_t *)syms + (size_t)i * symsh.sh_entsize);
            uint8_t bind = ELF64_ST_BIND(s->st_info);
            uint8_t typ = ELF64_ST_TYPE(s->st_info);
            if (s->st_shndx == SHN_UNDEF || s->st_name == 0) {
                continue;
            }
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            if (typ != STT_FUNC && typ != STT_NOTYPE) {
                continue;
            }
            if (sym_addr[i] == 0) {
                continue;
            }
            img->syms[w].name = str2 + s->st_name;
            img->syms[w].addr = (void *)sym_addr[i];
            img->syms[w].st_info = s->st_info;
            w++;
        }
    }

    MICROPY_WASM_FREE(sec_addr);
    MICROPY_WASM_FREE(got_off);
    MICROPY_WASM_FREE(sym_addr);
    *out = img;
    return true;
}

void *mp_wasm_elf_lookup(const mp_wasm_elf_image_t *img, const char *name) {
    if (img == NULL || name == NULL) {
        return NULL;
    }
    for (uint32_t i = 0; i < img->n_syms; ++i) {
        if (img->syms[i].name && strcmp(img->syms[i].name, name) == 0) {
            return img->syms[i].addr;
        }
    }
    return NULL;
}

void mp_wasm_elf_foreach_func(const mp_wasm_elf_image_t *img, mp_wasm_elf_export_cb cb, void *ctx) {
    if (img == NULL || cb == NULL) {
        return;
    }
    for (uint32_t i = 0; i < img->n_syms; ++i) {
        if (img->syms[i].name && img->syms[i].addr) {
            cb(img->syms[i].name, img->syms[i].addr, ctx);
        }
    }
}

#endif // MICROPY_PY_WASM && MICROPY_PY_WASM_ELF
