/*
 * Bind MPWP Python/mpy + export callables onto a loaded pack's sys.modules face.
 * Offline only — no CDN. See pack/__types__.h and docs/CALLGRAPH.md.
 */
#ifndef MICROPY_INCLUDED_WASMMOD_PORTS_PACKBIND_H
#define MICROPY_INCLUDED_WASMMOD_PORTS_PACKBIND_H

#include <stddef.h>
#include <stdint.h>

#include "py/obj.h"

#include "pymergetic/wasmmod/registry/__types__.h"

/* After loader_load / ELF publish: attach handle attrs, bind MPWP, exports. */
mp_obj_t mp_wasm_pack_bind(const char *pack_name, pm_wasmmod_registry_handle_t h,
    const uint8_t *meta, uint32_t meta_len);

/* Publish ELF ET_REL image into registry (CONTAINER_ELF) with i32 adapters.
 * On success *img_out receives the image (owned until mp_wasm_elf_release). */
pm_wasmmod_registry_handle_t mp_wasm_elf_publish(const char *pack_name,
    const uint8_t *bytes, uint32_t len, void **img_out, char *err, size_t err_len);
void mp_wasm_elf_release_for_module(mp_obj_t mod);

#endif /* MICROPY_INCLUDED_WASMMOD_PORTS_PACKBIND_H */
