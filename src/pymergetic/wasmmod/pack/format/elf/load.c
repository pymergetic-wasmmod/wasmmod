/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * In-tree ELF64/ELF32 ET_REL loader (x86_64 / aarch64 / arm). No dlopen / ld.so.
 *
 * Linked only when MICROPY_PY_WASM_ELF=1 (native seats). Browser/Emscripten
 * builds omit this file entirely (WASMMOD_EMSCRIPTEN → ELF=0).
 *
 * ELF32 (arm) objects come from the same one-file TCC recipe as the 64-bit
 * seats (tcc_instances.mk, TCC_TARGET_ARM) and — unlike gcc/clang — TCC's
 * arm backend emits SHT_REL with implicit addends: no r_addend field, the
 * addend is the word already at the patch site. The walk reads it out,
 * applies, writes the result back. The non-PIC reloc set TCC emits is small
 * (probed on a real object, not read from a table): ABS32, REL32, PC24 (BL
 * / b imm24), THM_CALL / THM_JUMP24 (thumb BL / b.w, encoding pair), MOVW
 * / MOVT (literal materialization, cpuver>=7), V4BX, TLS_LE32, NONE.
 * GOT-family relocs only appear under CONFIG_TCC_PIC, which no seat
 * defines; reloc_needs_got still knows R_ARM_GOT_PREL so a PIC object
 * fails politely (unresolved GOT slot), never silently.
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
#define EM_ARM 40
#define EM_X86_64 62
#define EM_AARCH64 183
#define ELFCLASS32 1
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

/* ARM (armv7) — the numbers from TCC's elf.h, verified against the real
 * objects the vendored backend emits (see the file header). PC24/THM_* are
 * the branch pair, MOVW/MOVT the literal pair, TLS_LE32 the only TLS form
 * a static link ever sees (THM_PC22 is the modern ABI's THM_CALL). */
#define R_ARM_NONE 0
#define R_ARM_PC24 1
#define R_ARM_ABS32 2
#define R_ARM_REL32 3
#define R_ARM_THM_PC22 10
#define R_ARM_CALL 28
#define R_ARM_JUMP24 29
#define R_ARM_THM_JUMP24 30
#define R_ARM_TARGET1 38
#define R_ARM_V4BX 40
#define R_ARM_MOVW_ABS_NC 43
#define R_ARM_MOVT_ABS 44
#define R_ARM_THM_MOVW_ABS_NC 47
#define R_ARM_THM_MOVT_ABS 48
#define R_ARM_GOT_PREL 96
#define R_ARM_TLS_LE32 108

#if defined(__aarch64__)
#define MP_WASM_ELF_HOST_EM EM_AARCH64
#elif defined(__x86_64__)
#define MP_WASM_ELF_HOST_EM EM_X86_64
#elif defined(__arm__) && !defined(__aarch64__)
#define MP_WASM_ELF_HOST_EM EM_ARM
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

/* ELF32 LE (arm) — same rule as the 64 structs: natural alignment matches
 * the on-disk layout, so no packing. Only the fields the loader reads. */
typedef struct {
    uint8_t e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
} Elf32_Sym;

/* SHT_REL entry — the implicit-addend form: r_info packs sym<<8|type in
 * ELF32 (not sym<<32 like ELF64). */
typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;

_Static_assert(sizeof(Elf32_Ehdr) == 52, "Elf32_Ehdr size");
_Static_assert(sizeof(Elf32_Shdr) == 40, "Elf32_Shdr size");
_Static_assert(sizeof(Elf32_Sym) == 16, "Elf32_Sym size");
_Static_assert(sizeof(Elf32_Rel) == 8, "Elf32_Rel size");

#define ELF32_ST_TYPE(i) ((i) & 0xf)
#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_R_SYM(i) ((i) >> 8)
#define ELF32_R_TYPE(i) ((i) & 0xff)

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
    if (em == EM_ARM) {
        return typ == R_ARM_GOT_PREL;
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

/* ---------------- ARM (armv7) patch helpers ----------------
 * Same shape as the aarch64 helpers: patch one instruction in place, range
 * check first, errbuf on refusal. The encodings are the 1997 ARM ABI ones
 * TCC emits — externals/tcc/arm-link.c is the reference for the math. */

/* BL / B (ARM state): imm24 = (S + A - (P + 8)) >> 2, sign 24 bits. The +8
 * is the archaic ARM PC bias (an ARM-state instruction reads PC as its own
 * address + 8); in TCC's form it rides inside A (the decoded imm24 already
 * carries -8), so the patch below folds it in exactly like TCC's
 * x += val - addr. A call to a thumb target would need BLX — TCC emits ARM
 * state functions only, so a bit-0 target is a refuse, not a patch. */
static bool arm_patch_branch24(uint32_t *ins, int64_t byte_off, char *errbuf, size_t errbuf_len) {
    if ((byte_off & 3) != 0) {
        set_err(errbuf, errbuf_len, "arm branch misaligned");
        return false;
    }
    int64_t imm = byte_off >> 2;
    if ((imm >> 23) != 0 && (imm >> 23) != -1) {
        set_err(errbuf, errbuf_len, "arm branch out of range");
        return false;
    }
    uint32_t i = *ins;
    i = (i & 0xff000000u) | ((uint32_t)imm & 0x00ffffffu);
    *ins = i;
    return true;
}

/* Thumb BL / B.W (encoding pair, THM_PC22 / THM_JUMP24): byte_off is the
 * S + A - P delta; imm24 = byte_off >> 1 packs across the two halfwords as
 * S:I1:I2:imm10:imm11 with J1/J2 storing the inverted I1/I2 XOR S — the
 * same forward rule TCC's relocate() walks backwards. Range ±16MiB like
 * TCC (x in [-0x1000000, 0x1000000)). The thumb PC bias (P+4) rides in A
 * exactly like the ARM one. */
static bool arm_patch_thm_branch22(uint16_t *hw, int64_t byte_off, char *errbuf, size_t errbuf_len) {
    if ((byte_off & 1) != 0) {
        set_err(errbuf, errbuf_len, "thumb branch misaligned");
        return false;
    }
    int64_t imm = byte_off >> 1;
    if (imm < -(1 << 23) || imm >= (1 << 23)) {
        set_err(errbuf, errbuf_len, "thumb branch out of range");
        return false;
    }
    {
        int64_t x = imm << 1;
        if (x >= 0x1000000 || x < -0x1000000) {
            set_err(errbuf, errbuf_len, "thumb branch out of range");
            return false;
        }
    }
    uint32_t s = (uint32_t)((imm >> 23) & 1);
    uint32_t i1 = (uint32_t)((imm >> 22) & 1);
    uint32_t i2 = (uint32_t)((imm >> 21) & 1);
    uint32_t j1 = (s ^ (i1 ^ 1)) & 1;
    uint32_t j2 = (s ^ (i2 ^ 1)) & 1;
    uint32_t imm10 = (uint32_t)((imm >> 11) & 0x3ff);
    uint32_t imm11 = (uint32_t)(imm & 0x7ff);
    uint16_t hi = hw[0];
    uint16_t lo = hw[1];
    hi = (uint16_t)((hi & 0xf800u) | (s << 10) | imm10);
    lo = (uint16_t)((lo & 0xd000u) | (j1 << 13) | (j2 << 11) | imm11);
    hw[0] = hi;
    hw[1] = lo;
    return true;
}

/* MOVW / MOVT (A1 encoding, ARM state): imm16 = imm4 (bits 16-19) : imm12
 * (bits 0-11) — the exact split TCC's relocate() writes ((imm4 << 12) |
 * imm12). MOVW takes the low 16 bits, MOVT the high ones; emitted only at
 * CONFIG_TCC_CPUVER >= 7 (the default 5 uses literal pools). */
static void arm_patch_imm16_a1(uint32_t *ins, uint32_t imm16) {
    uint32_t i = *ins;
    i &= ~((0xfu << 16) | 0x0fffu);
    i |= ((imm16 >> 12) & 0xfu) << 16;
    i |= imm16 & 0x0fffu;
    *ins = i;
}

/* One relocation, shared by the single- and multi-object loaders.
 * P: patch site (already base-adjusted). S: resolved symbol address.
 * got_base: image offset of the GOT. got_slot: this symbol's global GOT
 * slot index (only read for GOTPCREL-family and GOT32 relocs). Returns
 * false with errbuf set on overflow / unsupported / out-of-range.
 * em: the object's e_machine — the apply switch dispatches on it first
 * because ARM type numbers collide with x86_64's (R_ARM_PC24 == 1 ==
 * R_X86_64_64), so typ alone cannot route. */
static bool elf_apply_rela(uint16_t em, uint8_t *P, uintptr_t S, int64_t A, uint32_t typ,
    uint8_t *img_base, size_t got_base, uint32_t got_slot,
    char *errbuf, size_t errbuf_len) {
    if (em == EM_ARM) {
        switch (typ) {
            case R_ARM_NONE:
            case R_ARM_V4BX:
                break;
            case R_ARM_ABS32:
            case R_ARM_TARGET1: {
                uint64_t v = (uint64_t)(S + (uintptr_t)A);
                if (v > 0xffffffffu) {
                    set_err(errbuf, errbuf_len, "R_ARM_ABS32 overflow");
                    return false;
                }
                *(uint32_t *)P = (uint32_t)v;
                break;
            }
            case R_ARM_REL32: {
                int64_t v = (int64_t)S + A - (int64_t)(uintptr_t)P;
                *(int32_t *)P = (int32_t)v;
                break;
            }
            case R_ARM_PC24:
            case R_ARM_CALL:
            case R_ARM_JUMP24: {
                /* A is the decoded imm24 placeholder — for a BL site that is
                 * -8 (the ARM PC bias), folded in exactly like TCC's
                 * x += val - addr. No bias here: it rides in A. */
                int64_t off = (int64_t)S + A - (int64_t)(uintptr_t)P;
                if ((S & 1u) != 0u) {
                    set_err(errbuf, errbuf_len, "arm branch to thumb target");
                    return false;
                }
                if (!arm_patch_branch24((uint32_t *)P, off, errbuf, errbuf_len)) {
                    return false;
                }
                break;
            }
            case R_ARM_THM_PC22:
            case R_ARM_THM_JUMP24: {
                /* Same rule as PC24: A is the decoded pair placeholder (the
                 * thumb PC bias rides in it), the patch is S + A - P. */
                int64_t off = (int64_t)S + A - (int64_t)(uintptr_t)P;
                if (!arm_patch_thm_branch22((uint16_t *)P, off, errbuf, errbuf_len)) {
                    return false;
                }
                break;
            }
            case R_ARM_MOVW_ABS_NC:
            case R_ARM_THM_MOVW_ABS_NC:
                arm_patch_imm16_a1((uint32_t *)P, (uint32_t)((S + (uintptr_t)A) & 0xffffu));
                break;
            case R_ARM_MOVT_ABS:
            case R_ARM_THM_MOVT_ABS:
                arm_patch_imm16_a1((uint32_t *)P, (uint32_t)(((S + (uintptr_t)A) >> 16) & 0xffffu));
                break;
            case R_ARM_TLS_LE32: {
                /* value = TLS offset of the symbol in this image's block. S
                 * was redirected into the materialized TLS block at resolve
                 * time; the offset is what the r0-relative load computes. */
                int64_t v = (int64_t)S - (int64_t)(uintptr_t)tls_img_block;
                *(int32_t *)P = (int32_t)v;
                break;
            }
            default: {
                char msg[80];
                snprintf(msg, sizeof(msg), "unsupported arm reloc %u", (unsigned)typ);
                set_err(errbuf, errbuf_len, msg);
                return false;
            }
        }
        return true;
    }
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

/* ELF32 twins — same gates, same error strings (the seat cannot tell which
 * class it loaded from a failure message, and should not have to). */
static bool parse_ehdr32(const uint8_t *buf, uint32_t len, Elf32_Ehdr *eh, char *errbuf, size_t errbuf_len) {
    if (buf == NULL || len < sizeof(Elf32_Ehdr)) {
        set_err(errbuf, errbuf_len, "elf truncated");
        return false;
    }
    if (buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') {
        set_err(errbuf, errbuf_len, "not ELF");
        return false;
    }
    if (buf[4] != ELFCLASS32 || buf[5] != ELFDATA2LSB) {
        set_err(errbuf, errbuf_len, "need ELF32 LE");
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
    if (eh->e_shnum == 0 || eh->e_shentsize < sizeof(Elf32_Shdr)) {
        set_err(errbuf, errbuf_len, "bad section headers");
        return false;
    }
    uint64_t sh_end = (uint64_t)eh->e_shoff + (uint64_t)eh->e_shnum * eh->e_shentsize;
    if (eh->e_shoff >= len || sh_end > len) {
        set_err(errbuf, errbuf_len, "shdr out of range");
        return false;
    }
    return true;
}

static uintptr_t align_up(uintptr_t v, uintptr_t a) {
    if (a <= 1) {
        return v;
    }
    return (v + a - 1) & ~(a - 1);
}

/* ---------------- class-agnostic relocation reader ----------------
 * One entry shape for both classes and both section forms, so the walk
 * loops (single- and multi-object) never branch on class per entry:
 *
 *   SHT_RELA (any class): sym/type from r_info, addend = r_addend
 *   SHT_REL  (any class): same extraction, addend = the word already at
 *                   the patch site (implicit — TCC's arm backend is the
 *                   emitter: PTR_SIZE==8 → RELA, else REL, tcc.h)
 *
 * The implicit addend is read from the copied image (target_base) and
 * handed to elf_apply_rela exactly like an explicit one; the patch writes
 * the result back over it. Partial-field relocs (branch immediates) carry
 * their addend inside the immediate bits — decoded per type, the exact
 * inverse of what externals/tcc/arm-link.c folds back in. */
typedef struct {
    uint32_t sym;
    uint32_t typ;
    uint64_t off;
    int64_t addend;
} elf_rel_ent_t;

/* Bytes the patch writes at the site (0 = instruction-bits-only). Used by
 * both walks to reject writes past the target section's end. Machine-paired
 * like the apply switch: the ARM numbers collide with x86_64's, so the
 * default-4 branch must not run on an EM_ARM object. */
static size_t elf_reloc_write_size(uint16_t em, uint32_t typ) {
    if (em == EM_ARM) {
        switch (typ) {
            case R_ARM_NONE:
            case R_ARM_V4BX:
                return 0;
            case R_ARM_ABS32:
            case R_ARM_TARGET1:
            case R_ARM_REL32:
            case R_ARM_TLS_LE32:
                return 4;
            default:
                return 0;  /* branch/imm-field patches never widen the site */
        }
    }
    switch (typ) {
        case R_X86_64_NONE: // also R_AARCH64_NONE (0 on both)
            return 0;
        case R_X86_64_64:
        case R_AARCH64_ABS64:
        case R_X86_64_PC64:
        case R_AARCH64_PREL64:
            return 8;
        default:
            return 4;
    }
}

static bool elf_read_rel(const uint8_t *buf, uint32_t len, uint32_t sec_type,
    uint64_t sec_off, uint32_t ent_size, uint32_t r,
    uint8_t *target_base, uint32_t target_size, elf_rel_ent_t *ent) {
    uint64_t off = sec_off + (uint64_t)r * ent_size;
    if (ent == NULL || ent_size == 0 || off > len) {
        return false;
    }
    if (sec_type == SHT_RELA) {
        if (ent_size >= 24) {
            /* ELF64_Rela: r_offset, r_info (sym<<32|typ), r_addend. */
            const Elf64_Rela *rela;
            if (off + sizeof(Elf64_Rela) > len) {
                return false;
            }
            rela = (const Elf64_Rela *)(buf + off);
            ent->sym = (uint32_t)ELF64_R_SYM(rela->r_info);
            ent->typ = (uint32_t)ELF64_R_TYPE(rela->r_info);
            ent->off = rela->r_offset;
            ent->addend = rela->r_addend;
            return true;
        }
        {
            /* ELF32_Rela: r_offset, r_info (sym<<8|typ), r_addend — 12
             * bytes, natural alignment keeps the layout packed as written
             * (no emitter in this tree produces one; read anyway). */
            uint32_t w[3];
            if (off + 12 > len) {
                return false;
            }
            memcpy(w, buf + off, 12);
            ent->off = w[0];
            ent->sym = ELF32_R_SYM(w[1]);
            ent->typ = ELF32_R_TYPE(w[1]);
            ent->addend = (int32_t)w[2];
            return true;
        }
    }
    if (sec_type != SHT_REL) {
        return false;
    }
    if (ent_size >= 16) {
        /* ELF64_Rel (no emitter here either): sym<<32|typ, addend 0 — the
         * ELF64 world pairs Rel with RELA-style explicit addends, so an
         * ELF64 SHT_REL has no implicit addend to read. */
        uint32_t w[2];
        uint64_t info;
        if (off + 16 > len) {
            return false;
        }
        memcpy(w, buf + off, 8);
        memcpy(&info, buf + off + 8, 8);
        ent->off = ((uint64_t)w[1] << 32) | w[0];
        ent->sym = (uint32_t)ELF64_R_SYM(info);
        ent->typ = (uint32_t)ELF64_R_TYPE(info);
        ent->addend = 0;
        return true;
    }
    {
        /* ELF32_Rel — the arm form this loader exists for. */
        const Elf32_Rel *rel;
        uint32_t word;
        if (off + sizeof(Elf32_Rel) > len) {
            return false;
        }
        rel = (const Elf32_Rel *)(buf + off);
        ent->sym = ELF32_R_SYM(rel->r_info);
        ent->typ = ELF32_R_TYPE(rel->r_info);
        ent->off = rel->r_offset;
        /* Implicit addend: the on-disk word at the patch site. Full-word
         * relocs (ABS32/REL32/TLS_LE32) take it as-is. Partial-field
         * relocs carry it in their immediate bits — decoded per type, the
         * exact inverse of what arm-link.c's relocate() folds back in
         * (PC24: imm24<<2 sign-26; THM pair: the S:J1:J2:imm10:imm11
         * spread; MOVW/MOVT: the A1 imm4:imm12 split). */
        if (target_base == NULL || ent->off > target_size
            || target_size - ent->off < 4) {
            return false;
        }
        memcpy(&word, target_base + ent->off, 4);
        switch (ent->typ) {
            case R_ARM_PC24:
            case R_ARM_CALL:
            case R_ARM_JUMP24: {
                /* imm24<<2, sign-extended — the placeholder's own -8 (the
                 * ARM PC bias) rides inside it, exactly TCC's decode. The
                 * sign bit (imm24 bit 23) lands at bit 25 after <<2, so the
                 * correction is 1<<26 — half that (0x02000000) is the thumb
                 * pair's value and sign-mangles every backward BL. */
                uint32_t x = word & 0x00ffffffu;
                int32_t sx = (int32_t)(x << 2);
                if (x & 0x00800000u) {
                    sx -= 0x04000000;
                }
                ent->addend = sx;
                break;
            }
            case R_ARM_THM_PC22:
            case R_ARM_THM_JUMP24: {
                uint16_t hi = (uint16_t)(word & 0xffffu);
                uint16_t lo = (uint16_t)(word >> 16);
                uint32_t s = (hi >> 10) & 1u;
                uint32_t j1 = (lo >> 13) & 1u;
                uint32_t j2 = (lo >> 11) & 1u;
                uint32_t i1 = (j1 ^ s) ^ 1;
                uint32_t i2 = (j2 ^ s) ^ 1;
                int32_t x = (int32_t)((s << 24) | (i1 << 23) | (i2 << 22)
                    | ((hi & 0x3ffu) << 12) | ((lo & 0x7ffu) << 1));
                if (x & 0x01000000) {
                    x -= 0x02000000;
                }
                ent->addend = x;
                break;
            }
            case R_ARM_MOVW_ABS_NC:
            case R_ARM_MOVT_ABS:
            case R_ARM_THM_MOVW_ABS_NC:
            case R_ARM_THM_MOVT_ABS: {
                /* A1 encoding: imm16 = imm4<<12 | imm12 (bits 16-19, 0-11).
                 * The placeholder carries 0; decode for completeness. */
                uint32_t imm16 = ((word >> 4) & 0xf000u) | (word & 0x0fffu);
                ent->addend = (int32_t)imm16;
                break;
            }
            default:
                /* Full-word relocs and anything the patch treats as one. */
                ent->addend = (int32_t)word;
                break;
        }
        return true;
    }
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

/* ---------------- class-normalized object view ----------------
 * One view over an ELF64 or ELF32 ET_REL object: both load paths (single-
 * and multi-object) consume this, so the class difference (Ehdr/Shdr/Sym
 * layout, r_info packing, REL vs RELA) lives in exactly one place — these
 * accessors. Fill with elf_obj_parse(); every field read goes through
 * the elf_obj_* helpers, never through a raw Elf{32,64}_ struct again. */
typedef struct {
    const uint8_t *buf;
    uint32_t len;
    bool is32;
    uint16_t machine;
    uint16_t shnum;
    uint16_t shentsize;
    uint64_t shoff;
    int symtab_i;
    uint64_t sym_off;     /* symtab payload offset */
    uint32_t sym_entsize; /* per-entry stride */
    uint32_t nsym;
    uint64_t str_off;     /* strtab payload offset */
    uint32_t str_size;
} elf_obj_t;

typedef struct {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
    uint32_t entsize;
} elf_obj_shdr_t;

typedef struct {
    uint32_t name;
    uint64_t value;
    uint64_t size;
    uint8_t info;
    uint16_t shndx;
} elf_obj_sym_t;

/* Read section header i (class-normalized). */
static bool elf_obj_shdr(const elf_obj_t *o, uint16_t i, elf_obj_shdr_t *sh) {
    uint64_t off;
    if (sh == NULL || i >= o->shnum) {
        return false;
    }
    off = o->shoff + (uint64_t)i * o->shentsize;
    if (o->is32) {
        Elf32_Shdr s;
        if (off + sizeof(s) > o->len) {
            return false;
        }
        memcpy(&s, o->buf + off, sizeof(s));
        sh->name = s.sh_name;
        sh->type = s.sh_type;
        sh->flags = s.sh_flags;
        sh->offset = s.sh_offset;
        sh->size = s.sh_size;
        sh->link = s.sh_link;
        sh->info = s.sh_info;
        sh->addralign = s.sh_addralign;
        sh->entsize = s.sh_entsize;
        return true;
    }
    {
        Elf64_Shdr s;
        if (off + sizeof(s) > o->len) {
            return false;
        }
        memcpy(&s, o->buf + off, sizeof(s));
        sh->name = s.sh_name;
        sh->type = s.sh_type;
        sh->flags = s.sh_flags;
        sh->offset = s.sh_offset;
        sh->size = s.sh_size;
        sh->link = s.sh_link;
        sh->info = s.sh_info;
        sh->addralign = s.sh_addralign;
        sh->entsize = (uint32_t)s.sh_entsize;
        return true;
    }
}

/* Parse ehdr + locate symtab/strtab. Both class parsers gate the same way
 * (magic, class byte, ET_REL, host e_machine, shdr bounds) — the ONLY
 * class difference is which struct they memcpy. */
static bool elf_obj_parse(elf_obj_t *o, const uint8_t *buf, uint32_t len,
    char *errbuf, size_t errbuf_len) {
    uint16_t i;
    memset(o, 0, sizeof(*o));
    o->buf = buf;
    o->len = len;
    if (buf != NULL && len >= 5 && buf[4] == ELFCLASS32) {
        Elf32_Ehdr eh;
        if (!parse_ehdr32(buf, len, &eh, errbuf, errbuf_len)) {
            return false;
        }
        o->is32 = true;
        o->machine = eh.e_machine;
        o->shnum = eh.e_shnum;
        o->shentsize = eh.e_shentsize;
        o->shoff = eh.e_shoff;
    } else {
        Elf64_Ehdr eh;
        if (!parse_ehdr(buf, len, &eh, errbuf, errbuf_len)) {
            return false;
        }
        o->is32 = false;
        o->machine = eh.e_machine;
        o->shnum = eh.e_shnum;
        o->shentsize = eh.e_shentsize;
        o->shoff = eh.e_shoff;
    }
    /* Locate the symtab and validate both halves. */
    for (i = 0; i < o->shnum; ++i) {
        elf_obj_shdr_t sh;
        if (!elf_obj_shdr(o, i, &sh)) {
            set_err(errbuf, errbuf_len, "bad shdr");
            return false;
        }
        if (sh.type == SHT_SYMTAB) {
            elf_obj_shdr_t strsh;
            o->symtab_i = (int)i;
            if (sh.link >= o->shnum || !elf_obj_shdr(o, (uint16_t)sh.link, &strsh)
                || strsh.type != SHT_STRTAB
                || sh.offset + sh.size > len
                || strsh.offset + strsh.size > len
                || sh.entsize < (o->is32 ? sizeof(Elf32_Sym) : sizeof(Elf64_Sym))) {
                set_err(errbuf, errbuf_len, "bad symtab");
                return false;
            }
            o->sym_off = sh.offset;
            o->sym_entsize = (uint32_t)sh.entsize;
            o->nsym = (uint32_t)(sh.size / sh.entsize);
            o->str_off = strsh.offset;
            o->str_size = (uint32_t)strsh.size;
            return true;
        }
    }
    set_err(errbuf, errbuf_len, "no symtab");
    return false;
}

/* Read symbol k (class-normalized). */
static bool elf_obj_sym(const elf_obj_t *o, uint32_t k, elf_obj_sym_t *s) {
    uint64_t off;
    if (s == NULL || k >= o->nsym) {
        return false;
    }
    off = o->sym_off + (uint64_t)k * o->sym_entsize;
    if (o->is32) {
        Elf32_Sym e;
        if (off + sizeof(e) > o->len) {
            return false;
        }
        memcpy(&e, o->buf + off, sizeof(e));
        s->name = e.st_name;
        s->value = e.st_value;
        s->size = e.st_size;
        s->info = e.st_info;
        s->shndx = e.st_shndx;
        return true;
    }
    {
        Elf64_Sym e;
        if (off + sizeof(e) > o->len) {
            return false;
        }
        memcpy(&e, o->buf + off, sizeof(e));
        s->name = e.st_name;
        s->value = e.st_value;
        s->size = e.st_size;
        s->info = e.st_info;
        s->shndx = e.st_shndx;
        return true;
    }
}

/* Symbol name (in the object's strtab) or NULL. */
static const char *elf_obj_sym_name(const elf_obj_t *o, const elf_obj_sym_t *s) {
    if (s->name == 0 || s->name >= o->str_size) {
        return NULL;
    }
    return (const char *)(o->buf + o->str_off + s->name);
}

#define ELF_OBJ_ST_TYPE(i) ((i) & 0xf)
#define ELF_OBJ_ST_BIND(i) ((i) >> 4)

bool mp_wasm_elf_image_load(const uint8_t *elf, uint32_t len,
    mp_wasm_elf_sym_resolve_t resolve, void *resolve_ctx,
    mp_wasm_elf_image_t **out, char *errbuf, size_t errbuf_len) {
    mp_wasm_elf_resolve_clear_err();
    if (out == NULL) {
        return false;
    }
    *out = NULL;
    elf_obj_t o;
    if (!elf_obj_parse(&o, elf, len, errbuf, errbuf_len)) {
        return false;
    }

    // Layout ALLOC sections into a contiguous RWX image.
    uintptr_t *sec_addr = MICROPY_WASM_MALLOC((size_t)o.shnum * sizeof(uintptr_t));
    if (sec_addr == NULL) {
        set_err(errbuf, errbuf_len, "oom");
        return false;
    }
    memset(sec_addr, 0, (size_t)o.shnum * sizeof(uintptr_t));

    size_t image_size = 0;
    for (uint16_t i = 0; i < o.shnum; ++i) {
        elf_obj_shdr_t sh;
        if (!elf_obj_shdr(&o, i, &sh)) {
            MICROPY_WASM_FREE(sec_addr);
            set_err(errbuf, errbuf_len, "bad shdr");
            return false;
        }
        if (!(sh.flags & SHF_ALLOC) || sh.size == 0) {
            continue;
        }
        uintptr_t align = sh.addralign ? (uintptr_t)sh.addralign : 1;
        image_size = align_up(image_size, align);
        sec_addr[i] = image_size;
        image_size += (size_t)sh.size;
    }
    if (image_size == 0) {
        image_size = 4096;
    }

    // GOT slots for GOTPCREL* / ADR_GOT (even -fPIC -fno-plt uses these for undefs).
    uint32_t *got_off = MICROPY_WASM_MALLOC((size_t)o.nsym * sizeof(uint32_t));
    if (got_off == NULL) {
        MICROPY_WASM_FREE(sec_addr);
        set_err(errbuf, errbuf_len, "oom");
        return false;
    }
    memset(got_off, 0xff, (size_t)o.nsym * sizeof(uint32_t)); // ~0u = no slot
    uint32_t n_got = 0;
    for (uint16_t i = 0; i < o.shnum; ++i) {
        elf_obj_shdr_t sh;
        elf_obj_shdr(&o, i, &sh);
        if ((sh.type != SHT_RELA && sh.type != SHT_REL) || sh.entsize == 0
            || sh.offset + sh.size > len
            || sh.info >= o.shnum) {
            continue;
        }
        // Ignore .rela.debug_* etc. — target is not mapped into the image.
        elf_obj_shdr_t tgt;
        if (!elf_obj_shdr(&o, (uint16_t)sh.info, &tgt) || !(tgt.flags & SHF_ALLOC)) {
            continue;
        }
        uint32_t nrel = (uint32_t)(sh.size / sh.entsize);
        for (uint32_t r = 0; r < nrel; ++r) {
            elf_rel_ent_t ent;
            if (!elf_read_rel(o.buf, len, sh.type, sh.offset, (uint32_t)sh.entsize,
                r, NULL, 0, &ent)) {
                continue;
            }
            if (ent.sym >= o.nsym) {
                continue;
            }
            if (reloc_needs_got(o.machine, ent.typ)) {
                if (got_off[ent.sym] == ~0u) {
                    got_off[ent.sym] = n_got++;
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

    for (uint16_t i = 0; i < o.shnum; ++i) {
        elf_obj_shdr_t sh;
        elf_obj_shdr(&o, i, &sh);
        if (!(sh.flags & SHF_ALLOC) || sh.size == 0) {
            continue;
        }
        if (sh.type == SHT_NOBITS) {
            continue; // already zeroed
        }
        if (sh.offset + sh.size > len) {
            munmap(map, map_size);
            MICROPY_WASM_FREE(sec_addr);
            MICROPY_WASM_FREE(got_off);
            set_err(errbuf, errbuf_len, "section data OOB");
            return false;
        }
        memcpy(base + sec_addr[i], elf + sh.offset, (size_t)sh.size);
    }

    // Materialize the TLS block (if the object has TLS sections) so STT_TLS
    // symbols resolve into real thread storage, not the image template.
    {
        size_t tdata_off = 0, tdata_size = 0, tbss_off = 0, tbss_size = 0;
        int has_tls = 0;
        for (uint16_t i = 0; i < o.shnum; ++i) {
            elf_obj_shdr_t sh;
            elf_obj_shdr(&o, i, &sh);
            if ((sh.flags & SHF_TLS) == 0 || sh.size == 0) {
                continue;
            }
            has_tls = 1;
            if (sh.type == SHT_NOBITS) {
                tbss_off = sec_addr[i];
                tbss_size = (size_t)sh.size;
            } else {
                tdata_off = sec_addr[i];
                tdata_size = (size_t)sh.size;
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
    uintptr_t *sym_addr = MICROPY_WASM_MALLOC((size_t)o.nsym * sizeof(uintptr_t));
    if (sym_addr == NULL) {
        munmap(map, map_size);
        MICROPY_WASM_FREE(sec_addr);
        MICROPY_WASM_FREE(got_off);
        set_err(errbuf, errbuf_len, "oom");
        return false;
    }
    memset(sym_addr, 0, (size_t)o.nsym * sizeof(uintptr_t));
    for (uint32_t i = 0; i < o.nsym; ++i) {
        elf_obj_sym_t s;
        if (!elf_obj_sym(&o, i, &s)) {
            continue;
        }
        if (s.shndx == SHN_UNDEF) {
            const char *nm = elf_obj_sym_name(&o, &s);
            if (nm != NULL) {
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
        if (s.shndx == SHN_ABS) {
            sym_addr[i] = (uintptr_t)s.value;
            continue;
        }
        if (s.shndx == SHN_COMMON || s.shndx >= o.shnum) {
            continue;
        }
        sym_addr[i] = (uintptr_t)(base + sec_addr[s.shndx] + s.value);
        // STT_TLS: the value is an offset into the TLS segment, and the
        // real storage is the materialized block — redirect so TPOFF32
        // computes against it.
        if (ELF_OBJ_ST_TYPE(s.info) == STT_TLS && tls_img_block != NULL) {
            sym_addr[i] = (uintptr_t)tls_img_block + s.value;
        }
    }

    // Fill GOT entries with resolved symbol addresses.
    for (uint32_t i = 0; i < o.nsym; ++i) {
        if (got_off[i] == ~0u) {
            continue;
        }
        *(uint64_t *)(base + got_base + (size_t)got_off[i] * 8) = (uint64_t)sym_addr[i];
    }

    // Apply RELA/REL (ALLOC targets only — skip .rela.debug_* into unmapped secs).
    for (uint16_t i = 0; i < o.shnum; ++i) {
        elf_obj_shdr_t sh;
        elf_obj_shdr(&o, i, &sh);
        if (sh.type != SHT_RELA && sh.type != SHT_REL) {
            continue;
        }
        if (sh.info >= o.shnum || sh.entsize == 0
            || sh.offset + sh.size > len) {
            munmap(map, map_size);
            MICROPY_WASM_FREE(sec_addr);
            MICROPY_WASM_FREE(got_off);
            MICROPY_WASM_FREE(sym_addr);
            set_err(errbuf, errbuf_len, "bad reloc section");
            return false;
        }
        elf_obj_shdr_t tgt;
        if (!elf_obj_shdr(&o, (uint16_t)sh.info, &tgt) || !(tgt.flags & SHF_ALLOC)) {
            continue;
        }
        uint32_t nrel = (uint32_t)(sh.size / sh.entsize);
        uint8_t *target_base = base + sec_addr[sh.info];
        for (uint32_t r = 0; r < nrel; ++r) {
            elf_rel_ent_t ent;
            if (!elf_read_rel(o.buf, len, sh.type, sh.offset, (uint32_t)sh.entsize,
                r, target_base, (uint32_t)tgt.size, &ent)) {
                munmap(map, map_size);
                MICROPY_WASM_FREE(sec_addr);
                MICROPY_WASM_FREE(got_off);
                MICROPY_WASM_FREE(sym_addr);
                set_err(errbuf, errbuf_len, "bad reloc entry");
                return false;
            }
            if (ent.sym >= o.nsym) {
                munmap(map, map_size);
                MICROPY_WASM_FREE(sec_addr);
                MICROPY_WASM_FREE(got_off);
                MICROPY_WASM_FREE(sym_addr);
                set_err(errbuf, errbuf_len, "rela sym OOB");
                return false;
            }
            uintptr_t S = sym_addr[ent.sym];
            if (S == 0 && ent.typ != R_X86_64_NONE) {
                elf_obj_sym_t s;
                const char *nm = "?";
                char msg[160];
                if (elf_obj_sym(&o, ent.sym, &s)) {
                    nm = elf_obj_sym_name(&o, &s);
                    if (nm == NULL) {
                        nm = "?";
                    }
                }
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
                size_t need = elf_reloc_write_size(o.machine, ent.typ);
                if (need != 0
                    && (ent.off > tgt.size
                        || tgt.size - ent.off < need)) {
                    munmap(map, map_size);
                    MICROPY_WASM_FREE(sec_addr);
                    MICROPY_WASM_FREE(got_off);
                    MICROPY_WASM_FREE(sym_addr);
                    set_err(errbuf, errbuf_len, "rela offset OOB");
                    return false;
                }
            }
            uint8_t *P = target_base + ent.off;
            if (!elf_apply_rela(o.machine, P, S, ent.addend, ent.typ, base, got_base,
                got_off[ent.sym], errbuf, errbuf_len)) {
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
    for (uint32_t i = 0; i < o.nsym; ++i) {
        elf_obj_sym_t s;
        if (!elf_obj_sym(&o, i, &s)) {
            continue;
        }
        uint8_t bind = ELF_OBJ_ST_BIND(s.info);
        uint8_t typ = ELF_OBJ_ST_TYPE(s.info);
        if (s.shndx == SHN_UNDEF || s.name == 0) {
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
        for (uint32_t i = 0; i < o.nsym; ++i) {
            elf_obj_sym_t s;
            if (!elf_obj_sym(&o, i, &s)) {
                continue;
            }
            uint8_t bind = ELF_OBJ_ST_BIND(s.info);
            uint8_t typ = ELF_OBJ_ST_TYPE(s.info);
            if (s.shndx == SHN_UNDEF || s.name == 0) {
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
            img->syms[w].name = (const char *)(img->file_copy + o.str_off + s.name);
            img->syms[w].addr = (void *)sym_addr[i];
            img->syms[w].st_info = s.info;
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
    elf_obj_t obj;         /* class-normalized view (ELF64 or ELF32) */
    uintptr_t *sec_addr;   /* section offsets, image-relative (before image_base) */
    size_t image_base;     /* where this object's sections start in the image */
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
        if (!elf_obj_parse(&o[i].obj, objs[i], lens[i], errbuf, errbuf_len)) {
            goto fail;
        }
        o[i].sec_addr = MICROPY_WASM_MALLOC((size_t)o[i].obj.shnum * sizeof(uintptr_t));
        if (o[i].sec_addr == NULL) {
            set_err(errbuf, errbuf_len, "oom");
            goto fail;
        }
        memset(o[i].sec_addr, 0, (size_t)o[i].obj.shnum * sizeof(uintptr_t));
    }

    /* Layout sections + count GOT slots per object (global slot indices). */
    size_t image_size = 0;
    uint32_t total_got = 0;
    for (i = 0; i < n_objs; ++i) {
        o[i].image_base = image_size;
        for (uint16_t s = 0; s < o[i].obj.shnum; ++s) {
            elf_obj_shdr_t sh;
            if (!elf_obj_shdr(&o[i].obj, s, &sh)) {
                set_err(errbuf, errbuf_len, "bad shdr");
                goto fail;
            }
            if (!(sh.flags & SHF_ALLOC) || sh.size == 0) {
                continue;
            }
            uintptr_t align = sh.addralign ? (uintptr_t)sh.addralign : 1;
            image_size = align_up(image_size, align);
            o[i].sec_addr[s] = image_size - o[i].image_base;
            image_size += (size_t)sh.size;
        }
        o[i].got_off = MICROPY_WASM_MALLOC((size_t)o[i].obj.nsym * sizeof(uint32_t));
        if (o[i].got_off == NULL) {
            set_err(errbuf, errbuf_len, "oom");
            goto fail;
        }
        memset(o[i].got_off, 0xff, (size_t)o[i].obj.nsym * sizeof(uint32_t));
        o[i].n_got = 0;
        o[i].first_got = total_got;
        for (uint16_t s = 0; s < o[i].obj.shnum; ++s) {
            elf_obj_shdr_t sh;
            if (!elf_obj_shdr(&o[i].obj, s, &sh)) {
                continue;
            }
            if ((sh.type != SHT_RELA && sh.type != SHT_REL) || sh.entsize == 0
                || sh.offset + sh.size > o[i].obj.len
                || sh.info >= o[i].obj.shnum) {
                continue;
            }
            elf_obj_shdr_t tgt;
            if (!elf_obj_shdr(&o[i].obj, (uint16_t)sh.info, &tgt)
                || !(tgt.flags & SHF_ALLOC)) {
                continue;
            }
            uint32_t nrel = (uint32_t)(sh.size / sh.entsize);
            for (uint32_t r = 0; r < nrel; ++r) {
                elf_rel_ent_t ent;
                if (!elf_read_rel(o[i].obj.buf, o[i].obj.len, sh.type, sh.offset,
                    (uint32_t)sh.entsize, r, NULL, 0, &ent)) {
                    continue;
                }
                if (ent.sym >= o[i].obj.nsym) {
                    continue;
                }
                if (reloc_needs_got(o[i].obj.machine, ent.typ)
                    && o[i].got_off[ent.sym] == 0xffffffffu) {
                    o[i].got_off[ent.sym] = total_got++;
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
        for (uint16_t s = 0; s < o[i].obj.shnum; ++s) {
            elf_obj_shdr_t sh;
            elf_obj_shdr(&o[i].obj, s, &sh);
            if (!(sh.flags & SHF_ALLOC) || sh.size == 0) {
                continue;
            }
            if (sh.type == SHT_NOBITS) {
                continue;
            }
            if (sh.offset + sh.size > o[i].obj.len) {
                set_err(errbuf, errbuf_len, "section data OOB");
                goto fail;
            }
            memcpy(base + o[i].image_base + o[i].sec_addr[s],
                o[i].obj.buf + sh.offset, (size_t)sh.size);
        }
    }

    /* Collect defined globals across objects (first in objs order wins). */
    uint32_t defs_cap = 0;
    for (i = 0; i < n_objs; ++i) {
        defs_cap += o[i].obj.nsym;
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
            for (uint16_t s = 0; s < o[i].obj.shnum; ++s) {
                elf_obj_shdr_t sh;
                if (!elf_obj_shdr(&o[i].obj, s, &sh)) {
                    continue;
                }
                if ((sh.flags & SHF_TLS) == 0 || sh.size == 0) {
                    continue;
                }
                has_tls = 1;
                {
                    size_t off = o[i].image_base + o[i].sec_addr[s];
                    if (sh.type == SHT_NOBITS) {
                        tbss_off = off;
                        tbss_size = (size_t)sh.size;
                    } else {
                        tdata_off = off;
                        tdata_size = (size_t)sh.size;
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
        for (uint32_t k = 0; k < o[i].obj.nsym; ++k) {
            elf_obj_sym_t s;
            if (!elf_obj_sym(&o[i].obj, k, &s)) {
                continue;
            }
            uint8_t bind = ELF_OBJ_ST_BIND(s.info);
            const char *nm = elf_obj_sym_name(&o[i].obj, &s);
            if (nm == NULL) {
                continue;
            }
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            if (s.shndx == SHN_UNDEF || s.shndx == SHN_ABS
                || s.shndx == SHN_COMMON || s.shndx >= o[i].obj.shnum) {
                continue;
            }
            defs[n_def].name = nm;
            defs[n_def].addr = (uintptr_t)(base + o[i].image_base
                + o[i].sec_addr[s.shndx] + s.value);
            // cross-object TLS references go through the shared block too
            if (ELF_OBJ_ST_TYPE(s.info) == STT_TLS && tls_img_block != NULL) {
                defs[n_def].addr = (uintptr_t)tls_img_block + s.value;
            }
            n_def++;
        }
    }

    /* Resolve symbols per object: defined → image; undef → other objects,
     * then the resolve callback. Fill GOT entries. */
    for (i = 0; i < n_objs; ++i) {
        o[i].sym_addr = MICROPY_WASM_MALLOC((size_t)o[i].obj.nsym * sizeof(uintptr_t));
        if (o[i].sym_addr == NULL) {
            set_err(errbuf, errbuf_len, "oom");
            goto fail;
        }
        memset(o[i].sym_addr, 0, (size_t)o[i].obj.nsym * sizeof(uintptr_t));
        for (uint32_t k = 0; k < o[i].obj.nsym; ++k) {
            elf_obj_sym_t s;
            if (!elf_obj_sym(&o[i].obj, k, &s)) {
                continue;
            }
            if (s.shndx == SHN_UNDEF) {
                const char *nm = elf_obj_sym_name(&o[i].obj, &s);
                if (nm == NULL) {
                    continue;
                }
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
            if (s.shndx == SHN_ABS) {
                o[i].sym_addr[k] = (uintptr_t)s.value;
                continue;
            }
            if (s.shndx == SHN_COMMON || s.shndx >= o[i].obj.shnum) {
                continue;
            }
            o[i].sym_addr[k] = (uintptr_t)(base + o[i].image_base
                + o[i].sec_addr[s.shndx] + s.value);
            // STT_TLS: redirect into the materialized TLS block.
            if (ELF_OBJ_ST_TYPE(s.info) == STT_TLS && tls_img_block != NULL) {
                o[i].sym_addr[k] = (uintptr_t)tls_img_block + s.value;
            }
        }
        for (uint32_t k = 0; k < o[i].obj.nsym; ++k) {
            if (o[i].got_off[k] == 0xffffffffu) {
                continue;
            }
            *(uint64_t *)(base + got_base + (size_t)o[i].got_off[k] * 8)
                = (uint64_t)o[i].sym_addr[k];
        }
    }

    /* Apply relocations per object (shared elf_apply_rela). */
    for (i = 0; i < n_objs; ++i) {
        for (uint16_t s = 0; s < o[i].obj.shnum; ++s) {
            elf_obj_shdr_t sh;
            if (!elf_obj_shdr(&o[i].obj, s, &sh)) {
                continue;
            }
            if (sh.type != SHT_RELA && sh.type != SHT_REL) {
                continue;
            }
            if (sh.info >= o[i].obj.shnum || sh.entsize == 0
                || sh.offset + sh.size > o[i].obj.len) {
                set_err(errbuf, errbuf_len, "bad reloc section");
                goto fail;
            }
            elf_obj_shdr_t tgt;
            if (!elf_obj_shdr(&o[i].obj, (uint16_t)sh.info, &tgt)
                || !(tgt.flags & SHF_ALLOC)) {
                continue;
            }
            uint32_t nrel = (uint32_t)(sh.size / sh.entsize);
            uint8_t *target_base = base + o[i].image_base + o[i].sec_addr[sh.info];
            for (uint32_t r = 0; r < nrel; ++r) {
                elf_rel_ent_t ent;
                if (!elf_read_rel(o[i].obj.buf, o[i].obj.len, sh.type, sh.offset,
                    (uint32_t)sh.entsize, r, target_base, (uint32_t)tgt.size, &ent)) {
                    set_err(errbuf, errbuf_len, "bad reloc entry");
                    goto fail;
                }
                if (ent.sym >= o[i].obj.nsym) {
                    set_err(errbuf, errbuf_len, "rela sym OOB");
                    goto fail;
                }
                uintptr_t S = o[i].sym_addr[ent.sym];
                if (S == 0 && ent.typ != R_X86_64_NONE) {
                    elf_obj_sym_t sm;
                    const char *nm = "?";
                    char msg[160];
                    if (elf_obj_sym(&o[i].obj, ent.sym, &sm)) {
                        nm = elf_obj_sym_name(&o[i].obj, &sm);
                        if (nm == NULL) {
                            nm = "?";
                        }
                    }
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
                    size_t need = elf_reloc_write_size(o[i].obj.machine, ent.typ);
                    if (need != 0
                        && (ent.off > tgt.size || tgt.size - ent.off < need)) {
                        set_err(errbuf, errbuf_len, "rela offset OOB");
                        goto fail;
                    }
                }
                uint8_t *P = target_base + ent.off;
                if (!elf_apply_rela(o[i].obj.machine, P, S, ent.addend, ent.typ,
                    base, got_base, o[i].got_off[ent.sym], errbuf, errbuf_len)) {
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
            total_file += o[i].obj.len;
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
            memcpy(img->file_copy + off, o[i].obj.buf, o[i].obj.len);
            off += o[i].obj.len;
        }
        /* Count publishable symbols. */
        for (i = 0; i < n_objs; ++i) {
            for (uint32_t k = 0; k < o[i].obj.nsym; ++k) {
                elf_obj_sym_t s;
                if (!elf_obj_sym(&o[i].obj, k, &s)) {
                    continue;
                }
                uint8_t bind = ELF_OBJ_ST_BIND(s.info);
                uint8_t typ = ELF_OBJ_ST_TYPE(s.info);
                if (s.shndx == SHN_UNDEF || s.name == 0) {
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
             * strtab lives at its own str_off within its slice. */
            size_t curoff = 0;
            for (i = 0; i < n_objs; ++i) {
                for (uint32_t k = 0; k < o[i].obj.nsym; ++k) {
                    elf_obj_sym_t s;
                    if (!elf_obj_sym(&o[i].obj, k, &s)) {
                        continue;
                    }
                    uint8_t bind = ELF_OBJ_ST_BIND(s.info);
                    uint8_t typ = ELF_OBJ_ST_TYPE(s.info);
                    if (s.shndx == SHN_UNDEF || s.name == 0) {
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
                    img->syms[w].name = (const char *)(img->file_copy + curoff
                        + o[i].obj.str_off + s.name);
                    img->syms[w].addr = (void *)o[i].sym_addr[k];
                    img->syms[w].st_info = s.info;
                    w++;
                }
                curoff += o[i].obj.len;
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
