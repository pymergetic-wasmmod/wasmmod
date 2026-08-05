/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * Pack inspect: ELF .symtab (+ has_dwarf), locations, hex/op disasm, mpy-dis.
 */

#ifndef MICROPY_PY_WASM
#define MICROPY_PY_WASM (0)
#endif

#if MICROPY_PY_WASM

#include "extmod/wasmmod/inspect.h"

#include <stdio.h>
#include <string.h>

#include "extmod/wasmmod/format/common/format.h"
#include "extmod/wasmmod/format/elf/section.h"
#include "extmod/wasmmod/pack.h"

#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define STT_FUNC 2
#define STT_OBJECT 1
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#define STT_FILE 4

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
    if (mp_wasm_elf_find_section(buf, len, ".debug_info", &p, &plen) && plen > 0) {
        return true;
    }
    if (mp_wasm_elf_find_section(buf, len, "debug_line", &p, &plen) && plen > 0) {
        return true;
    }
    if (mp_wasm_elf_find_section(buf, len, "debug_info", &p, &plen) && plen > 0) {
        return true;
    }
    return false;
}

// Skip one importdesc; returns false on truncate.
static bool wasm_skip_importdesc(const uint8_t **p, const uint8_t *end) {
    if (*p >= end) {
        return false;
    }
    uint8_t kind = *(*p)++;
    uint32_t u = 0;
    if (kind == 0) {
        return mp_wasm_read_uleb(p, end, &u);
    }
    if (kind == 1) {
        if (*p >= end) {
            return false;
        }
        (*p)++;
        uint32_t flags = 0;
        if (!mp_wasm_read_uleb(p, end, &flags) || !mp_wasm_read_uleb(p, end, &u)) {
            return false;
        }
        if ((flags & 1) && !mp_wasm_read_uleb(p, end, &u)) {
            return false;
        }
        return true;
    }
    if (kind == 2) {
        uint32_t flags = 0;
        if (!mp_wasm_read_uleb(p, end, &flags) || !mp_wasm_read_uleb(p, end, &u)) {
            return false;
        }
        if ((flags & 1) && !mp_wasm_read_uleb(p, end, &u)) {
            return false;
        }
        return true;
    }
    if (kind == 3) {
        if (*p + 2 > end) {
            return false;
        }
        *p += 2;
        return true;
    }
    return false;
}

static bool inspect_symbols_wasm(const uint8_t *buf, uint32_t len,
    mp_wasm_sym_t *out, size_t cap, size_t *n_out) {
    // First pass: func-import count, code-section list index + entry offsets.
    uint32_t n_func_imports = 0;
    int32_t code_sec_index = -1;
    uint64_t code_off[64];
    uint64_t code_sz[64];
    uint32_t ncode = 0;
    const uint8_t *exp_payload = NULL;
    uint32_t exp_plen = 0;

    if (len < 8 || buf[0] != 0 || buf[1] != 'a' || buf[2] != 's' || buf[3] != 'm') {
        return true;
    }
    uint32_t i = 8;
    uint32_t sec_list_i = 0;
    while (i < len) {
        uint8_t sid = buf[i++];
        const uint8_t *p = buf + i;
        const uint8_t *end_all = buf + len;
        uint32_t slen = 0;
        if (!mp_wasm_read_uleb(&p, end_all, &slen) || p + slen > end_all) {
            break;
        }
        const uint8_t *start = p;
        const uint8_t *end = p + slen;
        i = (uint32_t)(end - buf);
        if (sid == 2) {
            const uint8_t *q = start;
            uint32_t nimp = 0;
            if (!mp_wasm_read_uleb(&q, end, &nimp)) {
                break;
            }
            for (uint32_t k = 0; k < nimp; ++k) {
                uint32_t mlen = 0, flen = 0;
                if (!mp_wasm_read_uleb(&q, end, &mlen) || q + mlen > end) {
                    break;
                }
                q += mlen;
                if (!mp_wasm_read_uleb(&q, end, &flen) || q + flen > end) {
                    break;
                }
                q += flen;
                if (q >= end) {
                    break;
                }
                if (*q == 0) {
                    n_func_imports++;
                }
                if (!wasm_skip_importdesc(&q, end)) {
                    break;
                }
            }
        } else if (sid == 10) {
            code_sec_index = (int32_t)sec_list_i;
            const uint8_t *q = start;
            uint32_t nc = 0;
            if (!mp_wasm_read_uleb(&q, end, &nc)) {
                break;
            }
            ncode = 0;
            for (uint32_t k = 0; k < nc && ncode < 64; ++k) {
                const uint8_t *entry = q;
                uint32_t size = 0;
                if (!mp_wasm_read_uleb(&q, end, &size) || q + size > end) {
                    break;
                }
                q += size;
                code_off[ncode] = (uint64_t)(entry - start);
                code_sz[ncode] = (uint64_t)(q - entry);
                ncode++;
            }
        } else if (sid == 7) {
            exp_payload = start;
            exp_plen = slen;
        }
        sec_list_i++;
    }

    if (exp_payload == NULL || cap == 0) {
        return true;
    }
    const uint8_t *p = exp_payload;
    const uint8_t *end = exp_payload + exp_plen;
    uint32_t nexp = 0;
    if (!mp_wasm_read_uleb(&p, end, &nexp)) {
        return true;
    }
    size_t n = 0;
    for (uint32_t ei = 0; ei < nexp && n < cap; ++ei) {
        uint32_t nlen = 0;
        if (!mp_wasm_read_uleb(&p, end, &nlen) || p + nlen > end) {
            break;
        }
        char namebuf[sizeof(out[0].name)];
        size_t copy = nlen;
        if (copy >= sizeof(namebuf)) {
            copy = sizeof(namebuf) - 1;
        }
        memcpy(namebuf, p, copy);
        namebuf[copy] = '\0';
        p += nlen;
        if (p >= end) {
            break;
        }
        uint8_t ekind = *p++;
        uint32_t idx = 0;
        if (!mp_wasm_read_uleb(&p, end, &idx)) {
            break;
        }
        mp_wasm_sym_t *o = &out[n++];
        memset(o, 0, sizeof(*o));
        copy_name(o->name, sizeof(o->name), namebuf);
        o->binding = 3;
        if (ekind == 0) {
            o->kind = 3; // export
            o->section_index = code_sec_index;
            uint32_t local = idx - n_func_imports;
            if (idx >= n_func_imports && local < ncode) {
                o->offset = code_off[local];
                o->size = code_sz[local];
            }
        } else {
            o->kind = 0;
            o->section_index = -1;
        }
    }
    if (n_out) {
        *n_out = n;
    }
    return true;
}

bool mp_wasm_inspect_symbols(const uint8_t *buf, uint32_t len,
    mp_wasm_sym_t *out, size_t cap, size_t *n_out) {
    if (n_out) {
        *n_out = 0;
    }
    if (buf == NULL || out == NULL || cap == 0) {
        return false;
    }
    mp_wasm_artifact_kind_t kind = mp_wasm_artifact_kind(buf, len);
    if (kind == MP_WASM_KIND_WASM) {
        return inspect_symbols_wasm(buf, len, out, cap, n_out);
    }
    if (kind != MP_WASM_KIND_ELF) {
        return true; // empty ok for AOT / unknown
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
        if (sym.st_shndx == SHN_UNDEF || sym.st_shndx == SHN_ABS
            || sym.st_shndx == SHN_COMMON || sym.st_shndx >= 0xff00
            || sym.st_name == 0 || sym.st_name >= stab_sz) {
            continue;
        }
        const char *nm = (const char *)(stab + sym.st_name);
        if (nm[0] == '\0' || nm[0] == '.') {
            continue;
        }
        if ((sym.st_info & 0xf) == STT_FILE) {
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

bool mp_wasm_inspect_locations(const uint8_t *buf, uint32_t len, const char *name,
    mp_wasm_loc_t *out, size_t cap, size_t *n_out) {
    if (n_out) {
        *n_out = 0;
    }
    if (out == NULL || cap == 0 || name == NULL || name[0] == '\0') {
        return false;
    }
    mp_wasm_sym_t syms[64];
    size_t nsym = 0;
    if (!mp_wasm_inspect_symbols(buf, len, syms, 64, &nsym)) {
        return false;
    }
    size_t n = 0;
    for (size_t i = 0; i < nsym && n < cap; ++i) {
        if (strcmp(syms[i].name, name) != 0) {
            continue;
        }
        if (syms[i].kind == 1 && syms[i].size > 0 && n < cap) {
            mp_wasm_loc_t tmp[4];
            size_t nl = 0;
            if (mp_wasm_inspect_addr2line(buf, len, syms[i].offset, tmp, 4, &nl)) {
                for (size_t j = 0; j < nl && n < cap; ++j) {
                    out[n++] = tmp[j];
                }
            }
        }
        // Append role=sym unless addr2line already returned the same row.
        bool have_sym = false;
        for (size_t k = 0; k < n; ++k) {
            if (out[k].role == 0 && out[k].line < 0 && strcmp(out[k].path, name) == 0) {
                have_sym = true;
                break;
            }
        }
        if (!have_sym && n < cap) {
            memset(&out[n], 0, sizeof(out[n]));
            copy_name(out[n].path, sizeof(out[n].path), name);
            out[n].line = -1;
            out[n].role = 0; // sym
            n++;
        }
        break;
    }
    if (n_out) {
        *n_out = n;
    }
    return true;
}

static void disasm_db_lines(const uint8_t *data, uint32_t data_len, uint32_t base,
    uint32_t limit, mp_wasm_disasm_line_t *out, size_t cap, size_t *n_out) {
    size_t n = 0;
    uint32_t end = data_len;
    if (limit > 0 && limit < end) {
        end = limit;
    }
    for (uint32_t i = 0; i < end && n < cap; i += 8) {
        uint32_t chunk = end - i;
        if (chunk > 8) {
            chunk = 8;
        }
        char hex[3 * 8 + 1];
        size_t hp = 0;
        for (uint32_t b = 0; b < chunk && hp + 3 < sizeof(hex); ++b) {
            hp += (size_t)snprintf(hex + hp, sizeof(hex) - hp, "%s%02x", b ? " " : "", data[i + b]);
        }
        memset(&out[n], 0, sizeof(out[n]));
        out[n].addr = base + i;
        snprintf(out[n].text, sizeof(out[n].text), "db %s", hex);
        n++;
    }
    if (n_out) {
        *n_out = n;
    }
}

static bool elf_section_payload(const uint8_t *buf, uint32_t len, uint32_t index,
    const uint8_t **payload, uint32_t *payload_len) {
    if (payload) {
        *payload = NULL;
    }
    if (payload_len) {
        *payload_len = 0;
    }
    if (len < 64 || buf[0] != 0x7f || buf[4] != 2 || buf[5] != 1) {
        return false;
    }
    uint64_t e_shoff;
    uint16_t e_shentsize, e_shnum;
    memcpy(&e_shoff, buf + 40, 8);
    memcpy(&e_shentsize, buf + 58, 2);
    memcpy(&e_shnum, buf + 60, 2);
    if (index >= e_shnum || e_shentsize < 64) {
        return false;
    }
    if (e_shoff + (uint64_t)(index + 1) * e_shentsize > len) {
        return false;
    }
    const uint8_t *sh = buf + e_shoff + (uint64_t)index * e_shentsize;
    uint64_t off, sz;
    memcpy(&off, sh + 24, 8);
    memcpy(&sz, sh + 32, 8);
    if (off + sz > len) {
        return false;
    }
    if (payload) {
        *payload = buf + off;
    }
    if (payload_len) {
        *payload_len = (uint32_t)sz;
    }
    return true;
}

static bool wasm_code_window(const uint8_t *buf, uint32_t len, uint32_t offset,
    uint32_t limit, const uint8_t **chunk, uint32_t *chunk_len) {
    if (chunk) {
        *chunk = NULL;
    }
    if (chunk_len) {
        *chunk_len = 0;
    }
    if (len < 8 || buf[0] != 0 || buf[1] != 'a' || buf[2] != 's' || buf[3] != 'm') {
        return false;
    }
    uint32_t i = 8;
    while (i < len) {
        uint8_t sid = buf[i++];
        uint32_t slen = 0, shift = 0;
        while (i < len) {
            uint8_t b = buf[i++];
            slen |= (uint32_t)(b & 0x7f) << shift;
            if ((b & 0x80) == 0) {
                break;
            }
            shift += 7;
            if (shift > 28) {
                return false;
            }
        }
        if (i + slen > len) {
            return false;
        }
        if (sid == 10) { // code
            if (offset >= slen) {
                return true;
            }
            uint32_t avail = slen - offset;
            if (limit > 0 && limit < avail) {
                avail = limit;
            }
            if (chunk) {
                *chunk = buf + i + offset;
            }
            if (chunk_len) {
                *chunk_len = avail;
            }
            return true;
        }
        i += slen;
    }
    return false;
}

bool mp_wasm_inspect_disasm(const uint8_t *buf, uint32_t len,
    uint32_t section_index, uint32_t offset, uint32_t limit,
    mp_wasm_disasm_line_t *out, size_t cap, size_t *n_out) {
    if (n_out) {
        *n_out = 0;
    }
    if (out == NULL || cap == 0 || buf == NULL) {
        return false;
    }
    if (limit == 0) {
        limit = 64;
    }
    if (mp_wasm_artifact_kind(buf, len) == MP_WASM_KIND_ELF) {
        const uint8_t *payload = NULL;
        uint32_t plen = 0;
        if (!elf_section_payload(buf, len, section_index, &payload, &plen)) {
            return true;
        }
        if (offset >= plen) {
            return true;
        }
        uint32_t avail = plen - offset;
        if (limit < avail) {
            avail = limit;
        }
        disasm_db_lines(payload + offset, avail, offset, avail, out, cap, n_out);
        return true;
    }
    if (len >= 4 && buf[0] == 0 && buf[1] == 'a' && buf[2] == 's' && buf[3] == 'm') {
        (void)section_index; // Wasm: window into code section body
        const uint8_t *chunk = NULL;
        uint32_t clen = 0;
        if (!wasm_code_window(buf, len, offset, limit, &chunk, &clen) || chunk == NULL) {
            return true;
        }
        size_t n = 0;
        for (uint32_t j = 0; j < clen && n < cap; ++j) {
            memset(&out[n], 0, sizeof(out[n]));
            out[n].addr = offset + j;
            snprintf(out[n].text, sizeof(out[n].text), "op_%02x", chunk[j]);
            n++;
            if (n >= 64) {
                break;
            }
        }
        if (n_out) {
            *n_out = n;
        }
        return true;
    }
    return true;
}

bool mp_wasm_inspect_mpy_disasm(const uint8_t *mpy, uint32_t len, uint32_t limit,
    mp_wasm_disasm_line_t *out, size_t cap, size_t *n_out) {
    if (n_out) {
        *n_out = 0;
    }
    if (out == NULL || cap == 0) {
        return false;
    }
    if (limit == 0) {
        limit = 80;
    }
    size_t n = 0;
    if (len < 4) {
        memset(&out[0], 0, sizeof(out[0]));
        out[0].addr = 0;
        snprintf(out[0].text, sizeof(out[0].text), "truncated mpy");
        if (n_out) {
            *n_out = 1;
        }
        return true;
    }
    memset(&out[n], 0, sizeof(out[n]));
    out[n].addr = 0;
    snprintf(out[n].text, sizeof(out[n].text), "mpy_hdr %02x %02x %02x %02x",
        mpy[0], mpy[1], mpy[2], mpy[3]);
    n++;
    uint32_t body = len - 4;
    if (limit < body) {
        body = limit;
    }
    for (uint32_t i = 0; i < body && n < cap; ++i) {
        memset(&out[n], 0, sizeof(out[n]));
        out[n].addr = 4 + i;
        snprintf(out[n].text, sizeof(out[n].text), "bc 0x%02x", mpy[4 + i]);
        n++;
    }
    if (n_out) {
        *n_out = n;
    }
    return true;
}

#endif // MICROPY_PY_WASM
