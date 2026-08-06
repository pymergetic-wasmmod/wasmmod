/*
 * This file is part of wasmmod, https://github.com/pymergetic/wasmmod
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
 *
 * Pack inspect for .wasm / .aot / .elf (and .mpy disasm): symbols, DWARF
 * hooks, locations, disassembly. Same surface as Python wasm.has_dwarf / …
 */

#ifndef PM_PM_WASMMOD_INSPECT_INSPECT_H_
#define PM_PM_WASMMOD_INSPECT_INSPECT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef PM_WASMMOD_INSPECT_NAME_MAX
#define PM_WASMMOD_INSPECT_NAME_MAX 96
#endif
#ifndef PM_WASMMOD_INSPECT_TEXT_MAX
#define PM_WASMMOD_INSPECT_TEXT_MAX 80
#endif

typedef struct pm_wasmmod_sym {
    char name[PM_WASMMOD_INSPECT_NAME_MAX];
    int32_t section_index; /* -1 if n/a */
    uint64_t offset;
    uint64_t size;
    uint8_t kind;    /* 0=other 1=func 2=data 3=export */
    uint8_t binding; /* 0=local 1=global 2=weak 3=export */
} pm_wasmmod_sym_t;

typedef struct pm_wasmmod_loc {
    char path[PM_WASMMOD_INSPECT_NAME_MAX];
    int32_t line; /* -1 if unknown */
    uint8_t role; /* 0=sym 1=dwarf 2=def 3=decl 4=twin */
} pm_wasmmod_loc_t;

typedef struct pm_wasmmod_disasm_line {
    uint64_t addr;
    char text[PM_WASMMOD_INSPECT_TEXT_MAX];
} pm_wasmmod_disasm_line_t;

bool pm_wasmmod_inspect_has_dwarf(const uint8_t *buf, uint32_t len);

bool pm_wasmmod_inspect_symbols(const uint8_t *buf, uint32_t len,
    pm_wasmmod_sym_t *out, size_t cap, size_t *n_out);

bool pm_wasmmod_inspect_addr2line(const uint8_t *buf, uint32_t len, uint64_t addr,
    pm_wasmmod_loc_t *out, size_t cap, size_t *n_out);

bool pm_wasmmod_inspect_locations(const uint8_t *buf, uint32_t len, const char *name,
    pm_wasmmod_loc_t *out, size_t cap, size_t *n_out);

/** section_index: ELF shndx or Wasm section list index; hex/op dump lines. */
bool pm_wasmmod_inspect_disasm(const uint8_t *buf, uint32_t len,
    uint32_t section_index, uint32_t offset, uint32_t limit,
    pm_wasmmod_disasm_line_t *out, size_t cap, size_t *n_out);

bool pm_wasmmod_inspect_mpy_disasm(const uint8_t *mpy, uint32_t len, uint32_t limit,
    pm_wasmmod_disasm_line_t *out, size_t cap, size_t *n_out);

#ifdef __cplusplus
}
#endif

#endif /* PM_PM_WASMMOD_INSPECT_INSPECT_H_ */
