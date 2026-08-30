/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * In-tree ELF64 ET_REL loader (x86_64 / aarch64). No dlopen / ld.so.
 *
 * Linked only when MICROPY_PY_WASM_ELF=1 (native seats). Browser/Emscripten
 * builds omit this file entirely (WASMMOD_EMSCRIPTEN → ELF=0).
 */

#ifndef MICROPY_PY_WASM_ELF
#define MICROPY_PY_WASM_ELF (0)
#endif

#if MICROPY_PY_WASM_ELF

#include "pymergetic/wasmmod/pack/format/elf/load.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "pymergetic/wasmmod/pack/alloc.h"

#define EI_NIDENT 16
#define ET_REL 1
#define EM_X86_64 62
#define EM_AARCH64 183
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
#define SHF_TLS 0x400
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
#define STT_TLS 6
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
#define R_X86_64_TPOFF32 23

#define R_AARCH64_NONE 0
#define R_AARCH64_ABS64 257
#define R_AARCH64_ABS32 258
#define R_AARCH64_PREL64 261
#define R_AARCH64_PREL32 262
#define R_AARCH64_ADR_PREL_PG_HI21 275
#define R_AARCH64_ADR_PREL_PG_HI21_NC 276
#define R_AARCH64_ADD_ABS_LO12_NC 277
#define R_AARCH64_LDST8_ABS_LO12_NC 278
#define R_AARCH64_JUMP26 282
#define R_AARCH64_CALL26 283
#define R_AARCH64_LDST16_ABS_LO12_NC 284
#define R_AARCH64_LDST32_ABS_LO12_NC 285
#define R_AARCH64_LDST64_ABS_LO12_NC 286
#define R_AARCH64_LDST128_ABS_LO12_NC 299
#define R_AARCH64_ADR_GOT_PAGE 311
#define R_AARCH64_LD64_GOT_LO12_NC 312

#if defined(__aarch64__)
#define MP_WASM_ELF_HOST_EM EM_AARCH64
#elif defined(__x86_64__)
#define MP_WASM_ELF_HOST_EM EM_X86_64
#else
#define MP_WASM_ELF_HOST_EM 0
#endif

#define MP_WASM_ELF_PAGE(addr) ((uint64_t)(addr) & ~0xfffull)

/* ELF64 LE layouts match natural alignment — no #pragma pack (clangd
 * mis-tracks pack push/pop across the file-scoped #if … #endif). */
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

_Static_assert(sizeof(Elf64_Ehdr) == 64, "Elf64_Ehdr size");
_Static_assert(sizeof(Elf64_Shdr) == 64, "Elf64_Shdr size");
_Static_assert(sizeof(Elf64_Sym) == 24, "Elf64_Sym size");
_Static_assert(sizeof(Elf64_Rela) == 24, "Elf64_Rela size");

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

static bool reloc_needs_got(uint16_t em, uint32_t typ) {
    if (em == EM_X86_64) {
        return typ == R_X86_64_GOTPCREL || typ == R_X86_64_GOTPCRELX
            || typ == R_X86_64_REX_GOTPCRELX || typ == R_X86_64_GOT32;
    }
    if (em == EM_AARCH64) {
        return typ == R_AARCH64_ADR_GOT_PAGE || typ == R_AARCH64_LD64_GOT_LO12_NC;
    }
    return false;
}

/* ---------------- TLS support (R_X86_64_TPOFF32) ----------------
 * Objects compiled with __thread carry .tdata/.tbss sections (SHF_ALLOC |
 * SHF_TLS, laid into the image like any ALLOC section) and reference the
 * symbols with TPOFF32: value = sym_addr - %fs_base.
 *
 * The loader materializes ONE real TLS block per image: allocated from the
 * heap, .tdata template copied in, .tbss zeroed, and every STT_TLS symbol
 * resolved into it. TPOFF32 then computes block-relative offsets against
 * %fs:0. The seats are single-fs-threaded, so thread == process and the
 * computation is exact; a threaded seat would need a per-thread block and
 * re-relocation (documented in the header). */
static __thread unsigned char *tls_img_block;
static size_t tls_img_block_used;

/* Read %fs:0 (the thread pointer). x86_64 only; other arches have no
 * TPOFF32 to serve. */
#if defined(__x86_64__)
static uintptr_t elf_fs_base(void) {
    uintptr_t fs;
    __asm__ volatile("movq %%fs:0, %0" : "=r"(fs));
    return fs;
}
#else
static uintptr_t elf_fs_base(void) {
    return 0;
}
#endif

/* Materialize this image's TLS block from its .tdata template (in the
 * already-copied image) and .tbss size. tdata_off is the section's IMAGE
 * offset (where the template bytes live); tbss_off is unused (NOBITS has no
 * bytes — the block's zero-fill covers it). The block layout is the
 * TLS-segment layout — .tdata at 0, .tbss after it — so a symbol's
 * st_value (section-relative) lands on the right byte. Returns the block
 * address, or 0 on failure / non-x86_64 hosts. The __thread slot holds
 * the CURRENT image's block — loads are not nested, so one slot serves
 * the whole link. */
static uintptr_t elf_tls_materialize(uint8_t *img, size_t tdata_off,
    size_t tdata_size, size_t tbss_off, size_t tbss_size) {
    unsigned char *b;
    size_t total;
    (void)tbss_off;
    if (elf_fs_base() == 0) {
        return 0;
    }
    total = tdata_size + tbss_size;
    if (total == 0) {
        return 0;
    }
    b = (unsigned char *)MICROPY_WASM_MALLOC(total);
    if (b == NULL) {
        return 0;
    }
    memset(b, 0, total);
    if (tdata_size != 0) {
        memcpy(b, img + tdata_off, tdata_size);
    }
    tls_img_block = b;
    tls_img_block_used = total;
    return (uintptr_t)b;
}

static void aarch64_patch_adrp(uint32_t *ins, int64_t page_imm) {
    // page_imm is signed page count (delta >> 12).
    uint32_t immlo = (uint32_t)(page_imm & 3);
    uint32_t immhi = (uint32_t)((page_imm >> 2) & 0x1fffff);
    uint32_t i = *ins;
    i &= ~((3u << 29) | (0x1fffffu << 5));
    i |= (immlo << 29) | (immhi << 5);
    *ins = i;
}

static void aarch64_patch_imm12(uint32_t *ins, uint32_t imm12) {
    uint32_t i = *ins;
    i &= ~(0xfffu << 10);
    i |= (imm12 & 0xfff) << 10;
    *ins = i;
}

static bool aarch64_patch_branch26(uint32_t *ins, int64_t byte_off, char *errbuf, size_t errbuf_len) {
    if ((byte_off & 3) != 0) {
        set_err(errbuf, errbuf_len, "aarch64 branch misaligned");
        return false;
    }
    int64_t imm = byte_off >> 2;
    if (imm < -(1 << 25) || imm >= (1 << 25)) {
        set_err(errbuf, errbuf_len, "aarch64 branch out of range");
        return false;
    }
    uint32_t i = *ins;
    i = (i & ~0x03ffffffu) | ((uint32_t)imm & 0x03ffffffu);
    *ins = i;
    return true;
}

/* One relocation, shared by the single- and multi-object loaders.
 * P: patch site (already base-adjusted). S: resolved symbol address.
 * got_base: image offset of the GOT. got_slot: this symbol's global GOT
 * slot index (only read for GOTPCREL-family and GOT32 relocs). Returns
 * false with errbuf set on overflow / unsupported / out-of-range. */
static bool elf_apply_rela(uint8_t *P, uintptr_t S, int64_t A, uint32_t typ,
    uint8_t *img_base, size_t got_base, uint32_t got_slot,
    char *errbuf, size_t errbuf_len) {
    switch (typ) {
        case R_X86_64_NONE: // also R_AARCH64_NONE
            break;
        case R_X86_64_64:
        case R_AARCH64_ABS64:
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
            uintptr_t G = (uintptr_t)(img_base + got_base + (size_t)got_slot * 8);
            int64_t v = (int64_t)G + A - (int64_t)(uintptr_t)P;
            *(int32_t *)P = (int32_t)v;
            break;
        }
        case R_X86_64_GOT32: {
            *(int32_t *)P = (int32_t)((int64_t)got_slot * 8 + A);
            break;
        }
        case R_X86_64_32:
        case R_X86_64_32S:
        case R_AARCH64_ABS32: {
            uint64_t v = (uint64_t)(S + (uintptr_t)A);
            if (v > 0xffffffffu) {
                set_err(errbuf, errbuf_len,
                    "R_*_32 overflow (build with -fPIC or map below 4GiB)");
                return false;
            }
            *(uint32_t *)P = (uint32_t)v;
            break;
        }
        case R_X86_64_TPOFF32: {
            /* value = sym - thread pointer. S was already redirected into
             * the materialized TLS block at symbol-resolution time, so the
             * subtraction lands the offset the fs-relative access needs. */
            int64_t v = (int64_t)S - (int64_t)elf_fs_base();
            *(int32_t *)P = (int32_t)v;
            break;
        }
        case R_X86_64_PC64:
        case R_AARCH64_PREL64: {
            int64_t v = (int64_t)S + A - (int64_t)(uintptr_t)P;
            *(int64_t *)P = v;
            break;
        }
        case R_AARCH64_PREL32: {
            int64_t v = (int64_t)S + A - (int64_t)(uintptr_t)P;
            *(int32_t *)P = (int32_t)v;
            break;
        }
        case R_AARCH64_ADR_PREL_PG_HI21:
        case R_AARCH64_ADR_PREL_PG_HI21_NC: {
            uint64_t dest = (uint64_t)(S + (uintptr_t)A);
            int64_t page_imm = (int64_t)((MP_WASM_ELF_PAGE(dest) - MP_WASM_ELF_PAGE((uint64_t)(uintptr_t)P)) >> 12);
            aarch64_patch_adrp((uint32_t *)P, page_imm);
            break;
        }
        case R_AARCH64_ADD_ABS_LO12_NC:
        case R_AARCH64_LDST8_ABS_LO12_NC:
            aarch64_patch_imm12((uint32_t *)P, (uint32_t)((S + (uintptr_t)A) & 0xfff));
            break;
        case R_AARCH64_LDST16_ABS_LO12_NC:
            aarch64_patch_imm12((uint32_t *)P, (uint32_t)(((S + (uintptr_t)A) & 0xfff) >> 1));
            break;
        case R_AARCH64_LDST32_ABS_LO12_NC:
            aarch64_patch_imm12((uint32_t *)P, (uint32_t)(((S + (uintptr_t)A) & 0xfff) >> 2));
            break;
        case R_AARCH64_LDST64_ABS_LO12_NC:
            aarch64_patch_imm12((uint32_t *)P, (uint32_t)(((S + (uintptr_t)A) & 0xfff) >> 3));
            break;
        case R_AARCH64_LDST128_ABS_LO12_NC:
            aarch64_patch_imm12((uint32_t *)P, (uint32_t)(((S + (uintptr_t)A) & 0xfff) >> 4));
            break;
        case R_AARCH64_JUMP26:
        case R_AARCH64_CALL26: {
            int64_t off = (int64_t)S + A - (int64_t)(uintptr_t)P;
            if (!aarch64_patch_branch26((uint32_t *)P, off, errbuf, errbuf_len)) {
                return false;
            }
            break;
        }
        case R_AARCH64_ADR_GOT_PAGE: {
            uintptr_t G = (uintptr_t)(img_base + got_base + (size_t)got_slot * 8);
            int64_t page_imm = (int64_t)((MP_WASM_ELF_PAGE(G) - MP_WASM_ELF_PAGE((uint64_t)(uintptr_t)P)) >> 12);
            aarch64_patch_adrp((uint32_t *)P, page_imm);
            break;
        }
        case R_AARCH64_LD64_GOT_LO12_NC: {
            uintptr_t G = (uintptr_t)(img_base + got_base + (size_t)got_slot * 8);
            aarch64_patch_imm12((uint32_t *)P, (uint32_t)((G & 0xfff) >> 3));
            break;
        }
        default: {
            char msg[80];
            snprintf(msg, sizeof(msg), "unsupported reloc %u", (unsigned)typ);
            set_err(errbuf, errbuf_len, msg);
            return false;
        }
    }
    return true;
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
    if (MP_WASM_ELF_HOST_EM == 0) {
        set_err(errbuf, errbuf_len, "ELF host arch unsupported");
        return false;
    }
    if (eh->e_machine != MP_WASM_ELF_HOST_EM) {
        set_err(errbuf, errbuf_len, "ELF e_machine != host arch");
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

static char elf_resolve_err[160];

void mp_wasm_elf_resolve_clear_err(void) {
    elf_resolve_err[0] = '\0';
}

void mp_wasm_elf_resolve_set_err(const char *msg) {
    if (msg == NULL || msg[0] == '\0') {
        elf_resolve_err[0] = '\0';
        return;
    }
    size_t n = strlen(msg);
    if (n >= sizeof(elf_resolve_err)) {
        n = sizeof(elf_resolve_err) - 1;
    }
    memcpy(elf_resolve_err, msg, n);
    elf_resolve_err[n] = '\0';
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
    if (img->tls_block) {
        MICROPY_WASM_FREE(img->tls_block);
    }
    MICROPY_WASM_FREE(img);
}

bool mp_wasm_elf_image_load(const uint8_t *elf, uint32_t len,
    mp_wasm_elf_sym_resolve_t resolve, void *resolve_ctx,
    mp_wasm_elf_image_t **out, char *errbuf, size_t errbuf_len) {
    mp_wasm_elf_resolve_clear_err();
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

    // GOT slots for GOTPCREL* / ADR_GOT (even -fPIC -fno-plt uses these for undefs).
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
            || sh.sh_offset + sh.sh_size > len
            || sh.sh_info >= eh.e_shnum) {
            continue;
        }
        // Ignore .rela.debug_* etc. — target is not mapped into the image.
        Elf64_Shdr tgt;
        if (!get_shdr(elf, len, &eh, sh.sh_info, &tgt) || !(tgt.sh_flags & SHF_ALLOC)) {
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
            if (reloc_needs_got(eh.e_machine, typ)) {
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
#if defined(__x86_64__) && defined(MAP_32BIT)
        // Prefer low 32-bit VA so R_X86_64_32 (-fno-pic .rodata) still works.
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
#else
        MAP_PRIVATE | MAP_ANONYMOUS,
#endif
        -1, 0);
    if (map == MAP_FAILED) {
#if defined(__x86_64__) && defined(MAP_32BIT)
        map = mmap(NULL, map_size, PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    }
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

    // Materialize the TLS block (if the object has TLS sections) so STT_TLS
    // symbols resolve into real thread storage, not the image template.
    {
        size_t tdata_off = 0, tdata_size = 0, tbss_off = 0, tbss_size = 0;
        int has_tls = 0;
        for (uint16_t i = 0; i < eh.e_shnum; ++i) {
            Elf64_Shdr sh;
            get_shdr(elf, len, &eh, i, &sh);
            if ((sh.sh_flags & SHF_TLS) == 0 || sh.sh_size == 0) {
                continue;
            }
            has_tls = 1;
            if (sh.sh_type == SHT_NOBITS) {
                tbss_off = sec_addr[i];
                tbss_size = (size_t)sh.sh_size;
            } else {
                tdata_off = sec_addr[i];
                tdata_size = (size_t)sh.sh_size;
            }
        }
        if (has_tls) {
            if (elf_tls_materialize(base, tdata_off, tdata_size,
                tbss_off, tbss_size) == 0) {
                munmap(map, map_size);
                MICROPY_WASM_FREE(sec_addr);
                MICROPY_WASM_FREE(got_off);
                set_err(errbuf, errbuf_len, "tls materialize failed");
                return false;
            }
        }
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
        // STT_TLS: the value is an offset into the TLS segment, and the
        // real storage is the materialized block — redirect so TPOFF32
        // computes against it.
        if (ELF64_ST_TYPE(s->st_info) == STT_TLS && tls_img_block != NULL) {
            sym_addr[i] = (uintptr_t)tls_img_block + s->st_value;
        }
    }

    // Fill GOT entries with resolved symbol addresses.
    for (uint32_t i = 0; i < nsym; ++i) {
        if (got_off[i] == ~0u) {
            continue;
        }
        *(uint64_t *)(base + got_base + (size_t)got_off[i] * 8) = (uint64_t)sym_addr[i];
    }

    // Apply RELA (ALLOC targets only — skip .rela.debug_* into unmapped secs).
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
        Elf64_Shdr tgt;
        if (!get_shdr(elf, len, &eh, sh.sh_info, &tgt) || !(tgt.sh_flags & SHF_ALLOC)) {
            continue;
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
                if (elf_resolve_err[0] != '\0') {
                    snprintf(msg, sizeof(msg), "%s", elf_resolve_err);
                    mp_wasm_elf_resolve_clear_err();
                } else {
                    snprintf(msg, sizeof(msg), "unresolved symbol: %s", nm);
                }
                munmap(map, map_size);
                MICROPY_WASM_FREE(sec_addr);
                MICROPY_WASM_FREE(got_off);
                MICROPY_WASM_FREE(sym_addr);
                set_err(errbuf, errbuf_len, msg);
                return false;
            }
            // Reject writes past the target section (8-byte vs 4-byte by type).
            {
                size_t need = 4;
                if (typ == R_X86_64_NONE) {
                    need = 0;
                } else if (typ == R_X86_64_64 || typ == R_AARCH64_ABS64
                    || typ == R_X86_64_PC64 || typ == R_AARCH64_PREL64) {
                    need = 8;
                }
                if (need != 0
                    && (rela->r_offset > tgt.sh_size
                        || tgt.sh_size - rela->r_offset < need)) {
                    munmap(map, map_size);
                    MICROPY_WASM_FREE(sec_addr);
                    MICROPY_WASM_FREE(got_off);
                    MICROPY_WASM_FREE(sym_addr);
                    set_err(errbuf, errbuf_len, "rela offset OOB");
                    return false;
                }
            }
            uint8_t *P = target_base + rela->r_offset;
            int64_t A = rela->r_addend;
            if (!elf_apply_rela(P, S, A, typ, base, got_base, got_off[sym_i],
                errbuf, errbuf_len)) {
                munmap(map, map_size);
                MICROPY_WASM_FREE(sec_addr);
                MICROPY_WASM_FREE(got_off);
                MICROPY_WASM_FREE(sym_addr);
                return false;
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

    // The TLS block transfers to the image (freed on image destroy). The
    // __thread slot is a load-scoped pointer — clear it so a nested load
    // cannot redirect a later symbol resolution into this block.
    img->tls_block = tls_img_block;
    img->tls_size = tls_img_block_used;
    tls_img_block = NULL;
    tls_img_block_used = 0;

    MICROPY_WASM_FREE(sec_addr);
    MICROPY_WASM_FREE(got_off);
    MICROPY_WASM_FREE(sym_addr);
    *out = img;
    return true;
}

/*
 * Multi-object load: N ET_REL objects into ONE executable image.
 *
 * Layout = every object's ALLOC sections appended (each object keeps its own
 * section offsets, rebased by that object's image_base), then one shared GOT.
 * Symbol resolution = defined globals collected across objects first (a name
 * resolves to the FIRST definition in objs order — pass objects in depends
 * order), then the resolve callback for true externals. Relocations reuse
 * elf_apply_rela; section bases are offset by image_base.
 */
typedef struct {
    const uint8_t *elf;
    uint32_t len;
    Elf64_Ehdr eh;
    uintptr_t *sec_addr;   /* section offsets, image-relative (before image_base) */
    size_t image_base;     /* where this object's sections start in the image */
    int symtab_i;
    Elf64_Shdr symsh;
    Elf64_Shdr strsh;
    uint32_t nsym;
    uintptr_t *sym_addr;   /* resolved absolute addresses */
    uint32_t *got_off;     /* per-symbol GLOBAL GOT slot (uint32)-1 = none */
    uint32_t n_got;
    uint32_t first_got;    /* this object's slots start here in the shared GOT */
} elf_multi_obj_t;

typedef struct {
    const char *name;
    uintptr_t addr;
} elf_multi_def_t;

static void multi_free_objs(elf_multi_obj_t *objs_st, uint32_t n_parsed) {
    for (uint32_t i = 0; i < n_parsed; ++i) {
        if (objs_st[i].sec_addr) {
            MICROPY_WASM_FREE(objs_st[i].sec_addr);
        }
        if (objs_st[i].sym_addr) {
            MICROPY_WASM_FREE(objs_st[i].sym_addr);
        }
        if (objs_st[i].got_off) {
            MICROPY_WASM_FREE(objs_st[i].got_off);
        }
    }
    MICROPY_WASM_FREE(objs_st);
}

static bool multi_lookup_def(const elf_multi_def_t *defs, uint32_t n_def,
    const char *nm, uintptr_t *out) {
    for (uint32_t d = 0; d < n_def; ++d) {
        if (defs[d].name != NULL && strcmp(defs[d].name, nm) == 0) {
            *out = defs[d].addr;
            return true;
        }
    }
    return false;
}

bool mp_wasm_elf_image_load_multi(const uint8_t *const *objs, const uint32_t *lens,
    uint32_t n_objs,
    mp_wasm_elf_sym_resolve_t resolve, void *resolve_ctx,
    mp_wasm_elf_image_t **out, char *errbuf, size_t errbuf_len) {
    mp_wasm_elf_resolve_clear_err();
    if (out == NULL || n_objs == 0 || objs == NULL || lens == NULL) {
        set_err(errbuf, errbuf_len, "multi: no objects");
        return false;
    }
    *out = NULL;

    elf_multi_obj_t *o = MICROPY_WASM_MALLOC((size_t)n_objs * sizeof(*o));
    if (o == NULL) {
        set_err(errbuf, errbuf_len, "oom");
        return false;
    }
    memset(o, 0, (size_t)n_objs * sizeof(*o));

    uint32_t i;
    void *map = NULL;
    size_t map_size = 0;
    elf_multi_def_t *defs = NULL;
    uint32_t n_def = 0;
    mp_wasm_elf_image_t *img = NULL;

    /* Parse every object, locate symtabs. */
    for (i = 0; i < n_objs; ++i) {
        o[i].elf = objs[i];
        o[i].len = lens[i];
        if (!parse_ehdr(o[i].elf, o[i].len, &o[i].eh, errbuf, errbuf_len)) {
            goto fail;
        }
        o[i].sec_addr = MICROPY_WASM_MALLOC((size_t)o[i].eh.e_shnum * sizeof(uintptr_t));
        if (o[i].sec_addr == NULL) {
            set_err(errbuf, errbuf_len, "oom");
            goto fail;
        }
        memset(o[i].sec_addr, 0, (size_t)o[i].eh.e_shnum * sizeof(uintptr_t));
        o[i].symtab_i = -1;
        for (uint16_t s = 0; s < o[i].eh.e_shnum; ++s) {
            Elf64_Shdr sh;
            if (get_shdr(o[i].elf, o[i].len, &o[i].eh, s, &sh)
                && sh.sh_type == SHT_SYMTAB) {
                o[i].symtab_i = (int)s;
                break;
            }
        }
        if (o[i].symtab_i < 0) {
            set_err(errbuf, errbuf_len, "no symtab");
            goto fail;
        }
        if (!get_shdr(o[i].elf, o[i].len, &o[i].eh, (uint16_t)o[i].symtab_i, &o[i].symsh)
            || !get_shdr(o[i].elf, o[i].len, &o[i].eh, (uint16_t)o[i].symsh.sh_link, &o[i].strsh)
            || o[i].strsh.sh_type != SHT_STRTAB
            || o[i].symsh.sh_offset + o[i].symsh.sh_size > o[i].len
            || o[i].strsh.sh_offset + o[i].strsh.sh_size > o[i].len
            || o[i].symsh.sh_entsize < sizeof(Elf64_Sym)) {
            set_err(errbuf, errbuf_len, "bad symtab");
            goto fail;
        }
        o[i].nsym = (uint32_t)(o[i].symsh.sh_size / o[i].symsh.sh_entsize);
    }

    /* Layout sections + count GOT slots per object (global slot indices). */
    size_t image_size = 0;
    uint32_t total_got = 0;
    for (i = 0; i < n_objs; ++i) {
        o[i].image_base = image_size;
        for (uint16_t s = 0; s < o[i].eh.e_shnum; ++s) {
            Elf64_Shdr sh;
            if (!get_shdr(o[i].elf, o[i].len, &o[i].eh, s, &sh)) {
                set_err(errbuf, errbuf_len, "bad shdr");
                goto fail;
            }
            if (!(sh.sh_flags & SHF_ALLOC) || sh.sh_size == 0) {
                continue;
            }
            uintptr_t align = sh.sh_addralign ? (uintptr_t)sh.sh_addralign : 1;
            image_size = align_up(image_size, align);
            o[i].sec_addr[s] = image_size - o[i].image_base;
            image_size += (size_t)sh.sh_size;
        }
        o[i].got_off = MICROPY_WASM_MALLOC((size_t)o[i].nsym * sizeof(uint32_t));
        if (o[i].got_off == NULL) {
            set_err(errbuf, errbuf_len, "oom");
            goto fail;
        }
        memset(o[i].got_off, 0xff, (size_t)o[i].nsym * sizeof(uint32_t));
        o[i].n_got = 0;
        o[i].first_got = total_got;
        for (uint16_t s = 0; s < o[i].eh.e_shnum; ++s) {
            Elf64_Shdr sh;
            if (!get_shdr(o[i].elf, o[i].len, &o[i].eh, s, &sh)) {
                continue;
            }
            if (sh.sh_type != SHT_RELA || sh.sh_entsize < sizeof(Elf64_Rela)
                || sh.sh_offset + sh.sh_size > o[i].len
                || sh.sh_info >= o[i].eh.e_shnum) {
                continue;
            }
            Elf64_Shdr tgt;
            if (!get_shdr(o[i].elf, o[i].len, &o[i].eh, sh.sh_info, &tgt)
                || !(tgt.sh_flags & SHF_ALLOC)) {
                continue;
            }
            uint32_t nrel = (uint32_t)(sh.sh_size / sh.sh_entsize);
            for (uint32_t r = 0; r < nrel; ++r) {
                const Elf64_Rela *rela = (const Elf64_Rela *)(o[i].elf + sh.sh_offset + (size_t)r * sh.sh_entsize);
                uint32_t typ = (uint32_t)ELF64_R_TYPE(rela->r_info);
                uint32_t sym_i = (uint32_t)ELF64_R_SYM(rela->r_info);
                if (sym_i >= o[i].nsym) {
                    continue;
                }
                if (reloc_needs_got(o[i].eh.e_machine, typ)
                    && o[i].got_off[sym_i] == 0xffffffffu) {
                    o[i].got_off[sym_i] = total_got++;
                    o[i].n_got++;
                }
            }
        }
    }
    if (image_size == 0) {
        image_size = 4096;
    }

    size_t got_base = align_up(image_size, 8);
    image_size = got_base + (size_t)total_got * 8;

    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    map_size = align_up(image_size, page);
    map = mmap(NULL, map_size, PROT_READ | PROT_WRITE | PROT_EXEC,
#if defined(__x86_64__) && defined(MAP_32BIT)
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
#else
        MAP_PRIVATE | MAP_ANONYMOUS,
#endif
        -1, 0);
    if (map == MAP_FAILED) {
#if defined(__x86_64__) && defined(MAP_32BIT)
        map = mmap(NULL, map_size, PROT_READ | PROT_WRITE | PROT_EXEC,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    }
    if (map == MAP_FAILED) {
        map = NULL;
        set_err(errbuf, errbuf_len, "mmap failed");
        goto fail;
    }
    memset(map, 0, map_size);
    uint8_t *base = (uint8_t *)map;

    /* Copy ALLOC section data, rebased per object. */
    for (i = 0; i < n_objs; ++i) {
        for (uint16_t s = 0; s < o[i].eh.e_shnum; ++s) {
            Elf64_Shdr sh;
            get_shdr(o[i].elf, o[i].len, &o[i].eh, s, &sh);
            if (!(sh.sh_flags & SHF_ALLOC) || sh.sh_size == 0) {
                continue;
            }
            if (sh.sh_type == SHT_NOBITS) {
                continue;
            }
            if (sh.sh_offset + sh.sh_size > o[i].len) {
                set_err(errbuf, errbuf_len, "section data OOB");
                goto fail;
            }
            memcpy(base + o[i].image_base + o[i].sec_addr[s],
                o[i].elf + sh.sh_offset, (size_t)sh.sh_size);
        }
    }

    /* Collect defined globals across objects (first in objs order wins). */
    uint32_t defs_cap = 0;
    for (i = 0; i < n_objs; ++i) {
        defs_cap += o[i].nsym;
    }
    defs = MICROPY_WASM_MALLOC((size_t)defs_cap * sizeof(*defs));
    if (defs == NULL) {
        set_err(errbuf, errbuf_len, "oom");
        goto fail;
    }

    /* Materialize the shared TLS block across every object's TLS sections
     * (offsets are image-relative, so one block covers all objects). */
    tls_img_block = NULL;
    {
        size_t tdata_off = 0, tdata_size = 0, tbss_off = 0, tbss_size = 0;
        int has_tls = 0;
        for (i = 0; i < n_objs; ++i) {
            for (uint16_t s = 0; s < o[i].eh.e_shnum; ++s) {
                Elf64_Shdr sh;
                if (!get_shdr(o[i].elf, o[i].len, &o[i].eh, s, &sh)) {
                    continue;
                }
                if ((sh.sh_flags & SHF_TLS) == 0 || sh.sh_size == 0) {
                    continue;
                }
                has_tls = 1;
                {
                    size_t off = o[i].image_base + o[i].sec_addr[s];
                    if (sh.sh_type == SHT_NOBITS) {
                        tbss_off = off;
                        tbss_size = (size_t)sh.sh_size;
                    } else {
                        tdata_off = off;
                        tdata_size = (size_t)sh.sh_size;
                    }
                }
            }
        }
        if (has_tls) {
            if (elf_tls_materialize(base, tdata_off, tdata_size,
                tbss_off, tbss_size) == 0) {
                set_err(errbuf, errbuf_len, "tls materialize failed");
                goto fail;
            }
        }
    }

    for (i = 0; i < n_objs; ++i) {
        const Elf64_Sym *syms = (const Elf64_Sym *)(o[i].elf + o[i].symsh.sh_offset);
        const char *strtab = (const char *)(o[i].elf + o[i].strsh.sh_offset);
        for (uint32_t k = 0; k < o[i].nsym; ++k) {
            const Elf64_Sym *s = (const Elf64_Sym *)((const uint8_t *)syms + (size_t)k * o[i].symsh.sh_entsize);
            uint8_t bind = ELF64_ST_BIND(s->st_info);
            if (s->st_name == 0 || s->st_name >= o[i].strsh.sh_size) {
                continue;
            }
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            if (s->st_shndx == SHN_UNDEF || s->st_shndx == SHN_ABS
                || s->st_shndx == SHN_COMMON || s->st_shndx >= o[i].eh.e_shnum) {
                continue;
            }
            defs[n_def].name = strtab + s->st_name;
            defs[n_def].addr = (uintptr_t)(base + o[i].image_base
                + o[i].sec_addr[s->st_shndx] + s->st_value);
            // cross-object TLS references go through the shared block too
            if (ELF64_ST_TYPE(s->st_info) == STT_TLS && tls_img_block != NULL) {
                defs[n_def].addr = (uintptr_t)tls_img_block + s->st_value;
            }
            n_def++;
        }
    }

    /* Resolve symbols per object: defined → image; undef → other objects,
     * then the resolve callback. Fill GOT entries. */
    for (i = 0; i < n_objs; ++i) {
        const Elf64_Sym *syms = (const Elf64_Sym *)(o[i].elf + o[i].symsh.sh_offset);
        const char *strtab = (const char *)(o[i].elf + o[i].strsh.sh_offset);
        o[i].sym_addr = MICROPY_WASM_MALLOC((size_t)o[i].nsym * sizeof(uintptr_t));
        if (o[i].sym_addr == NULL) {
            set_err(errbuf, errbuf_len, "oom");
            goto fail;
        }
        memset(o[i].sym_addr, 0, (size_t)o[i].nsym * sizeof(uintptr_t));
        for (uint32_t k = 0; k < o[i].nsym; ++k) {
            const Elf64_Sym *s = (const Elf64_Sym *)((const uint8_t *)syms + (size_t)k * o[i].symsh.sh_entsize);
            if (s->st_shndx == SHN_UNDEF) {
                if (s->st_name == 0 || s->st_name >= o[i].strsh.sh_size) {
                    continue;
                }
                const char *nm = strtab + s->st_name;
                if (strcmp(nm, "_GLOBAL_OFFSET_TABLE_") == 0) {
                    o[i].sym_addr[k] = (uintptr_t)(base + got_base);
                    continue;
                }
                uintptr_t a = 0;
                if (multi_lookup_def(defs, n_def, nm, &a)) {
                    o[i].sym_addr[k] = a;
                } else if (resolve != NULL && nm[0] != '\0') {
                    void *p = resolve(nm, resolve_ctx);
                    if (p != NULL) {
                        o[i].sym_addr[k] = (uintptr_t)p;
                    }
                }
                continue;
            }
            if (s->st_shndx == SHN_ABS) {
                o[i].sym_addr[k] = (uintptr_t)s->st_value;
                continue;
            }
            if (s->st_shndx == SHN_COMMON || s->st_shndx >= o[i].eh.e_shnum) {
                continue;
            }
            o[i].sym_addr[k] = (uintptr_t)(base + o[i].image_base
                + o[i].sec_addr[s->st_shndx] + s->st_value);
            // STT_TLS: redirect into the materialized TLS block.
            if (ELF64_ST_TYPE(s->st_info) == STT_TLS && tls_img_block != NULL) {
                o[i].sym_addr[k] = (uintptr_t)tls_img_block + s->st_value;
            }
        }
        for (uint32_t k = 0; k < o[i].nsym; ++k) {
            if (o[i].got_off[k] == 0xffffffffu) {
                continue;
            }
            *(uint64_t *)(base + got_base + (size_t)o[i].got_off[k] * 8)
                = (uint64_t)o[i].sym_addr[k];
        }
    }

    /* Apply relocations per object (shared elf_apply_rela). */
    for (i = 0; i < n_objs; ++i) {
        for (uint16_t s = 0; s < o[i].eh.e_shnum; ++s) {
            Elf64_Shdr sh;
            if (!get_shdr(o[i].elf, o[i].len, &o[i].eh, s, &sh)) {
                continue;
            }
            if (sh.sh_type != SHT_RELA) {
                continue;
            }
            if (sh.sh_info >= o[i].eh.e_shnum || sh.sh_entsize < sizeof(Elf64_Rela)
                || sh.sh_offset + sh.sh_size > o[i].len) {
                set_err(errbuf, errbuf_len, "bad rela");
                goto fail;
            }
            Elf64_Shdr tgt;
            if (!get_shdr(o[i].elf, o[i].len, &o[i].eh, sh.sh_info, &tgt)
                || !(tgt.sh_flags & SHF_ALLOC)) {
                continue;
            }
            uint32_t nrel = (uint32_t)(sh.sh_size / sh.sh_entsize);
            uint8_t *target_base = base + o[i].image_base + o[i].sec_addr[sh.sh_info];
            for (uint32_t r = 0; r < nrel; ++r) {
                const Elf64_Rela *rela = (const Elf64_Rela *)(o[i].elf + sh.sh_offset + (size_t)r * sh.sh_entsize);
                uint32_t sym_i = (uint32_t)ELF64_R_SYM(rela->r_info);
                uint32_t typ = (uint32_t)ELF64_R_TYPE(rela->r_info);
                if (sym_i >= o[i].nsym) {
                    set_err(errbuf, errbuf_len, "rela sym OOB");
                    goto fail;
                }
                uintptr_t S = o[i].sym_addr[sym_i];
                if (S == 0 && typ != R_X86_64_NONE) {
                    const Elf64_Sym *syms = (const Elf64_Sym *)(o[i].elf + o[i].symsh.sh_offset);
                    const char *strtab = (const char *)(o[i].elf + o[i].strsh.sh_offset);
                    const Elf64_Sym *sm = (const Elf64_Sym *)((const uint8_t *)syms + (size_t)sym_i * o[i].symsh.sh_entsize);
                    const char *nm = (sm->st_name < o[i].strsh.sh_size) ? (strtab + sm->st_name) : "?";
                    char msg[160];
                    if (elf_resolve_err[0] != '\0') {
                        snprintf(msg, sizeof(msg), "%s", elf_resolve_err);
                        mp_wasm_elf_resolve_clear_err();
                    } else {
                        snprintf(msg, sizeof(msg), "multi: unresolved symbol: %s", nm);
                    }
                    set_err(errbuf, errbuf_len, msg);
                    goto fail;
                }
                {
                    size_t need = 4;
                    if (typ == R_X86_64_NONE) {
                        need = 0;
                    } else if (typ == R_X86_64_64 || typ == R_AARCH64_ABS64
                        || typ == R_X86_64_PC64 || typ == R_AARCH64_PREL64) {
                        need = 8;
                    }
                    if (need != 0
                        && (rela->r_offset > tgt.sh_size
                            || tgt.sh_size - rela->r_offset < need)) {
                        set_err(errbuf, errbuf_len, "rela offset OOB");
                        goto fail;
                    }
                }
                uint8_t *P = target_base + rela->r_offset;
                if (!elf_apply_rela(P, S, rela->r_addend, typ, base, got_base,
                    o[i].got_off[sym_i], errbuf, errbuf_len)) {
                    goto fail;
                }
            }
        }
    }

    /* Publish global function symbols (same shape as the single-object
     * loader; names copied from the LAST object's defs — but names must
     * outlive the ELF buffers, so copy all objects' bytes into file_copy). */
    {
        size_t total_file = 0;
        for (i = 0; i < n_objs; ++i) {
            total_file += o[i].len;
        }
        img = MICROPY_WASM_MALLOC(sizeof(*img));
        if (img == NULL) {
            set_err(errbuf, errbuf_len, "oom");
            goto fail;
        }
        memset(img, 0, sizeof(*img));
        img->base = base;
        img->size = map_size;
        img->file_copy = MICROPY_WASM_MALLOC(total_file);
        if (img->file_copy == NULL) {
            set_err(errbuf, errbuf_len, "oom");
            goto fail;
        }
        img->file_len = (uint32_t)total_file;

        /* Rebuild defs against the file_copy buffer so published names point
         * at owned memory. Walk each object's copy at its cumulative offset. */
        size_t off = 0;
        uint32_t n_pub = 0;
        for (i = 0; i < n_objs; ++i) {
            memcpy(img->file_copy + off, o[i].elf, o[i].len);
            off += o[i].len;
        }
        /* Count publishable symbols. */
        for (i = 0; i < n_objs; ++i) {
            const Elf64_Sym *syms = (const Elf64_Sym *)(o[i].elf + o[i].symsh.sh_offset);
            for (uint32_t k = 0; k < o[i].nsym; ++k) {
                const Elf64_Sym *s = (const Elf64_Sym *)((const uint8_t *)syms + (size_t)k * o[i].symsh.sh_entsize);
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
                if (o[i].sym_addr[k] == 0) {
                    continue;
                }
                n_pub++;
            }
        }
        img->n_syms = n_pub;
        if (n_pub > 0) {
            img->syms = MICROPY_WASM_MALLOC((size_t)n_pub * sizeof(mp_wasm_elf_sym_t));
            if (img->syms == NULL) {
                set_err(errbuf, errbuf_len, "oom");
                goto fail;
            }
            uint32_t w = 0;
            /* Object file copies are at cumulative offsets; each object's
             * strtab lives at its own sh_offset within its slice. */
            size_t curoff = 0;
            for (i = 0; i < n_objs; ++i) {
                const uint8_t *cpy = img->file_copy + curoff;
                const char *str2 = (const char *)(cpy + o[i].strsh.sh_offset);
                const Elf64_Sym *syms = (const Elf64_Sym *)(o[i].elf + o[i].symsh.sh_offset);
                for (uint32_t k = 0; k < o[i].nsym; ++k) {
                    const Elf64_Sym *s = (const Elf64_Sym *)((const uint8_t *)syms + (size_t)k * o[i].symsh.sh_entsize);
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
                    if (o[i].sym_addr[k] == 0) {
                        continue;
                    }
                    img->syms[w].name = str2 + s->st_name;
                    img->syms[w].addr = (void *)o[i].sym_addr[k];
                    img->syms[w].st_info = s->st_info;
                    w++;
                }
                curoff += o[i].len;
            }
        }
    }

    /* The TLS block transfers to the image (freed on destroy); clear the
     * load-scoped slot so a nested load cannot redirect into it. */
    img->tls_block = tls_img_block;
    img->tls_size = tls_img_block_used;
    tls_img_block = NULL;
    tls_img_block_used = 0;

    MICROPY_WASM_FREE(defs);
    multi_free_objs(o, n_objs);
    *out = img;
    return true;

fail:
    if (img != NULL) {
        mp_wasm_elf_image_free(img);
    } else if (map != NULL) {
        munmap(map, map_size);
    }
    if (tls_img_block != NULL) {
        MICROPY_WASM_FREE(tls_img_block);
        tls_img_block = NULL;
        tls_img_block_used = 0;
    }
    if (defs != NULL) {
        MICROPY_WASM_FREE(defs);
    }
    multi_free_objs(o, n_objs);
    return false;
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

#endif // MICROPY_PY_WASM_ELF
