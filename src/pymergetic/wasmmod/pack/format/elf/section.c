/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * ELF64 LE section find / strip / append for wasmmod.* metadata.
 * Write path uses SHT_PROGBITS named ".wasmmod.*"; read also accepts SHT_NOTE.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include "pymergetic/wasmmod/pack/format/elf/section.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pymergetic/wasmmod/pack/alloc.h"

#define EI_NIDENT 16
#define ET_REL 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOTE 7
#define SHT_NOBITS 8
#define SHT_REL 9

/* ELF64 LE — natural alignment; avoid #pragma pack across #if (clangd). */
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

_Static_assert(sizeof(Elf64_Ehdr) == 64, "Elf64_Ehdr size");
_Static_assert(sizeof(Elf64_Shdr) == 64, "Elf64_Shdr size");

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static void wr64(uint8_t *p, uint64_t v) {
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

static bool elf64_ok(const uint8_t *buf, uint32_t len, Elf64_Ehdr *eh) {
    if (buf == NULL || len < sizeof(Elf64_Ehdr)) {
        return false;
    }
    if (buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') {
        return false;
    }
    if (buf[4] != ELFCLASS64 || buf[5] != ELFDATA2LSB) {
        return false;
    }
    memcpy(eh, buf, sizeof(*eh));
    if (eh->e_ehsize < sizeof(Elf64_Ehdr) || eh->e_shentsize < sizeof(Elf64_Shdr)) {
        return false;
    }
    if (eh->e_shnum == 0 || eh->e_shstrndx >= eh->e_shnum) {
        return false;
    }
    uint64_t sh_end = eh->e_shoff + (uint64_t)eh->e_shnum * eh->e_shentsize;
    if (eh->e_shoff >= len || sh_end > len) {
        return false;
    }
    return true;
}

static bool shdr_at(const uint8_t *buf, uint32_t len, const Elf64_Ehdr *eh, uint16_t i, Elf64_Shdr *sh) {
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

static const char *sh_name_str(const uint8_t *buf, uint32_t len, const Elf64_Ehdr *eh,
    const Elf64_Shdr *shstr, uint32_t name_off) {
    if (shstr->sh_type != SHT_STRTAB) {
        return NULL;
    }
    if (shstr->sh_offset >= len || name_off >= shstr->sh_size) {
        return NULL;
    }
    uint64_t base = shstr->sh_offset + name_off;
    if (base >= len) {
        return NULL;
    }
    const char *s = (const char *)(buf + base);
    // Ensure NUL within section.
    uint64_t max = shstr->sh_size - name_off;
    for (uint64_t i = 0; i < max && base + i < len; ++i) {
        if (s[i] == '\0') {
            return s;
        }
    }
    return NULL;
}

static bool name_matches(const char *sec, const char *want) {
    if (sec == NULL || want == NULL) {
        return false;
    }
    if (strcmp(sec, want) == 0) {
        return true;
    }
    // Logical "wasmmod.pack" ↔ stored ".wasmmod.pack"
    if (want[0] != '.' && sec[0] == '.' && strcmp(sec + 1, want) == 0) {
        return true;
    }
    if (want[0] == '.' && sec[0] != '.' && strcmp(sec, want + 1) == 0) {
        return true;
    }
    return false;
}

static bool section_type_ok(uint32_t sh_type) {
    return sh_type == SHT_PROGBITS || sh_type == SHT_NOTE;
}

bool mp_wasm_elf_find_section(const uint8_t *buf, uint32_t len, const char *name,
    const uint8_t **payload, uint32_t *payload_len) {
    Elf64_Ehdr eh;
    if (!elf64_ok(buf, len, &eh) || name == NULL) {
        return false;
    }
    Elf64_Shdr shstr;
    if (!shdr_at(buf, len, &eh, eh.e_shstrndx, &shstr)) {
        return false;
    }
    for (uint16_t i = 0; i < eh.e_shnum; ++i) {
        Elf64_Shdr sh;
        if (!shdr_at(buf, len, &eh, i, &sh)) {
            return false;
        }
        if (!section_type_ok(sh.sh_type) || sh.sh_size == 0) {
            continue;
        }
        const char *sn = sh_name_str(buf, len, &eh, &shstr, sh.sh_name);
        if (!name_matches(sn, name)) {
            continue;
        }
        if (sh.sh_offset + sh.sh_size > len) {
            return false;
        }
        *payload = buf + sh.sh_offset;
        *payload_len = (uint32_t)sh.sh_size;
        return true;
    }
    return false;
}

#define WPSE_MAGIC_0 'W'
#define WPSE_MAGIC_1 'P'
#define WPSE_MAGIC_2 'S'
#define WPSE_MAGIC_3 'E'
#define WPSE_SIZE 28

static bool wpse_read(const uint8_t *buf, uint32_t len, uint64_t *old_len, uint64_t *old_shoff,
    uint16_t *old_shnum, uint16_t *old_shstrndx) {
    if (len < WPSE_SIZE) {
        return false;
    }
    const uint8_t *c = buf + len - WPSE_SIZE;
    if (c[0] != WPSE_MAGIC_0 || c[1] != WPSE_MAGIC_1 || c[2] != WPSE_MAGIC_2 || c[3] != WPSE_MAGIC_3) {
        return false;
    }
    uint64_t ol = (uint64_t)c[4] | ((uint64_t)c[5] << 8) | ((uint64_t)c[6] << 16) | ((uint64_t)c[7] << 24)
        | ((uint64_t)c[8] << 32) | ((uint64_t)c[9] << 40) | ((uint64_t)c[10] << 48) | ((uint64_t)c[11] << 56);
    uint64_t os = (uint64_t)c[12] | ((uint64_t)c[13] << 8) | ((uint64_t)c[14] << 16) | ((uint64_t)c[15] << 24)
        | ((uint64_t)c[16] << 32) | ((uint64_t)c[17] << 40) | ((uint64_t)c[18] << 48) | ((uint64_t)c[19] << 56);
    uint16_t on = (uint16_t)c[20] | ((uint16_t)c[21] << 8);
    uint16_t ox = (uint16_t)c[22] | ((uint16_t)c[23] << 8);
    if (ol == 0 || ol > len - WPSE_SIZE) {
        return false;
    }
    *old_len = ol;
    *old_shoff = os;
    *old_shnum = on;
    *old_shstrndx = ox;
    return true;
}

// Prefer WPSE cookie restore (exact pre-append bytes for sign digests).
bool mp_wasm_elf_strip_section(const uint8_t *buf, uint32_t len, const char *name,
    uint8_t **out, uint32_t *out_len) {
    Elf64_Ehdr eh;
    if (!elf64_ok(buf, len, &eh) || name == NULL) {
        return false;
    }
    Elf64_Shdr shstr;
    if (!shdr_at(buf, len, &eh, eh.e_shstrndx, &shstr)) {
        return false;
    }

    int drop = -1;
    for (uint16_t i = 1; i < eh.e_shnum; ++i) {
        Elf64_Shdr sh;
        if (!shdr_at(buf, len, &eh, i, &sh)) {
            return false;
        }
        if (!section_type_ok(sh.sh_type)) {
            continue;
        }
        const char *sn = sh_name_str(buf, len, &eh, &shstr, sh.sh_name);
        if (name_matches(sn, name)) {
            drop = (int)i;
            break;
        }
    }
    if (drop < 0) {
        // Nothing to strip — copy without trailing cookie if present.
        uint64_t ol, os;
        uint16_t on, ox;
        uint32_t copy_len = len;
        if (wpse_read(buf, len, &ol, &os, &on, &ox)) {
            copy_len = len - WPSE_SIZE;
        }
        uint8_t *copy = MICROPY_WASM_MALLOC(copy_len ? copy_len : 1);
        if (copy == NULL) {
            return false;
        }
        if (copy_len) {
            memcpy(copy, buf, copy_len);
        }
        *out = copy;
        *out_len = copy_len;
        return true;
    }

    uint64_t old_len, old_shoff;
    uint16_t old_shnum, old_shstrndx;
    if (wpse_read(buf, len, &old_len, &old_shoff, &old_shnum, &old_shstrndx)) {
        Elf64_Shdr drop_sh;
        if (shdr_at(buf, len, &eh, (uint16_t)drop, &drop_sh)
            && drop_sh.sh_offset >= old_len) {
            uint8_t *dst = MICROPY_WASM_MALLOC((size_t)old_len);
            if (dst == NULL) {
                return false;
            }
            memcpy(dst, buf, (size_t)old_len);
            wr64(dst + 40, old_shoff);
            wr16(dst + 60, old_shnum);
            wr16(dst + 62, old_shstrndx);
            *out = dst;
            *out_len = (uint32_t)old_len;
            return true;
        }
    }
    return false;
}

bool mp_wasm_elf_append_section(const uint8_t *buf, uint32_t len, const char *name,
    const uint8_t *payload, uint32_t payload_len, uint8_t **out, uint32_t *out_len) {
    Elf64_Ehdr eh;
    if (!elf64_ok(buf, len, &eh) || name == NULL || payload == NULL) {
        return false;
    }
    char dotted[128];
    const char *sec_name = name;
    if (name[0] != '.') {
        if (snprintf(dotted, sizeof(dotted), ".%s", name) >= (int)sizeof(dotted)) {
            return false;
        }
        sec_name = dotted;
    }
    size_t namelen = strlen(sec_name) + 1;

    Elf64_Shdr shstr;
    if (!shdr_at(buf, len, &eh, eh.e_shstrndx, &shstr)) {
        return false;
    }
    if (shstr.sh_type != SHT_STRTAB || shstr.sh_offset + shstr.sh_size > len) {
        return false;
    }

    uint64_t in_len = len;
    uint64_t discard_ol, discard_os;
    uint16_t discard_on, discard_ox;
    if (wpse_read(buf, len, &discard_ol, &discard_os, &discard_on, &discard_ox)) {
        in_len = len - WPSE_SIZE;
        if (!elf64_ok(buf, (uint32_t)in_len, &eh)) {
            return false;
        }
        if (!shdr_at(buf, (uint32_t)in_len, &eh, eh.e_shstrndx, &shstr)) {
            return false;
        }
    }
    uint64_t old_len = in_len;
    uint64_t old_shoff = eh.e_shoff;
    uint16_t old_shnum = eh.e_shnum;
    uint16_t old_shstrndx = eh.e_shstrndx;

    uint16_t new_shnum = (uint16_t)(eh.e_shnum + 1);
    uint64_t new_str_size = shstr.sh_size + namelen;

    size_t cap = (size_t)in_len + (size_t)payload_len + namelen + sizeof(Elf64_Shdr) + 64 + WPSE_SIZE;
    uint8_t *dst = MICROPY_WASM_MALLOC(cap);
    if (dst == NULL) {
        return false;
    }
    memcpy(dst, buf, (size_t)in_len);
    uint64_t cursor = in_len;
    // Align payload
    cursor = (cursor + 15) & ~15ull;
    if (cursor + payload_len + new_str_size + (uint64_t)new_shnum * sizeof(Elf64_Shdr) + WPSE_SIZE + 32 > cap) {
        size_t ncap = (size_t)cursor + (size_t)payload_len + (size_t)new_str_size
            + (size_t)new_shnum * sizeof(Elf64_Shdr) + WPSE_SIZE + 32;
        uint8_t *nd = MICROPY_WASM_REALLOC(dst, ncap);
        if (nd == NULL) {
            MICROPY_WASM_FREE(dst);
            return false;
        }
        dst = nd;
        cap = ncap;
    }
    uint64_t payload_off = cursor;
    memcpy(dst + cursor, payload, payload_len);
    cursor += payload_len;

    // New shstrtab = old + name
    cursor = (cursor + 7) & ~7ull;
    uint64_t new_shstr_off = cursor;
    memcpy(dst + cursor, buf + shstr.sh_offset, (size_t)shstr.sh_size);
    uint32_t name_idx = (uint32_t)shstr.sh_size;
    memcpy(dst + cursor + shstr.sh_size, sec_name, namelen);
    cursor += new_str_size;

    // Build new section headers: copy old with updated shstrtab, append new.
    Elf64_Shdr *hdrs = MICROPY_WASM_MALLOC((size_t)new_shnum * sizeof(Elf64_Shdr));
    if (hdrs == NULL) {
        MICROPY_WASM_FREE(dst);
        return false;
    }
    for (uint16_t i = 0; i < eh.e_shnum; ++i) {
        shdr_at(buf, (uint32_t)in_len, &eh, i, &hdrs[i]);
        if (i == eh.e_shstrndx) {
            hdrs[i].sh_offset = new_shstr_off;
            hdrs[i].sh_size = new_str_size;
        }
    }
    Elf64_Shdr *ns = &hdrs[eh.e_shnum];
    memset(ns, 0, sizeof(*ns));
    ns->sh_name = name_idx;
    ns->sh_type = SHT_PROGBITS;
    ns->sh_flags = 0;
    ns->sh_offset = payload_off;
    ns->sh_size = payload_len;
    ns->sh_addralign = 1;

    cursor = (cursor + 7) & ~7ull;
    uint64_t shoff = cursor;
    for (uint16_t i = 0; i < new_shnum; ++i) {
        memcpy(dst + cursor, &hdrs[i], sizeof(Elf64_Shdr));
        cursor += sizeof(Elf64_Shdr);
    }
    MICROPY_WASM_FREE(hdrs);

    wr64(dst + 40, shoff); // e_shoff
    wr16(dst + 60, new_shnum);
    // WPSE cookie for exact strip restore
    dst[cursor + 0] = WPSE_MAGIC_0;
    dst[cursor + 1] = WPSE_MAGIC_1;
    dst[cursor + 2] = WPSE_MAGIC_2;
    dst[cursor + 3] = WPSE_MAGIC_3;
    wr64(dst + cursor + 4, old_len);
    wr64(dst + cursor + 12, old_shoff);
    wr16(dst + cursor + 20, old_shnum);
    wr16(dst + cursor + 22, old_shstrndx);
    wr32(dst + cursor + 24, 0);
    cursor += WPSE_SIZE;

    *out = dst;
    *out_len = (uint32_t)cursor;
    return true;
}

#endif // MICROPY_PY_WASM
