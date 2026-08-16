/*
 * Pack path finder for the mpwm µPy host (ports/micropython).
 *
 * Searches wasm.path then sys.path for a .wasm artifact matching a dotted
 * import name. Forms tried (under each root):
 *   <dotted>.wasm          (flat — matches examples/packs FQN.wasm)
 *   <slash>.wasm           (a/b/c.wasm)
 *   <slash>/__init__.wasm
 *
 * Host faces (pymergetic.wasmmod / .upy / .metal) are never treated as
 * guest packs. Containers: .elf / .aotN / .aot / .wasm (+ .zlib).
 * HTTP roots on wasm.path / sys.path use pymergetic.wasmmod.io (probe to
 * find, fetch to load). Local roots stay on µPy VFS. Configured metal-cdn
 * bases skip flat HTTP probes; import uses net.cdn artifacts/lead|pin.
 */

#ifndef MICROPY_INCLUDED_WASMMOD_PORTS_FINDER_H
#define MICROPY_INCLUDED_WASMMOD_PORTS_FINDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "py/misc.h"
#include "py/obj.h"

#include "pymergetic/wasmmod/registry/__types__.h"

bool mp_wasm_is_host_face(const char *dotted_name);

/* Search wasm.path then sys.path (file + HTTP roots). On success fills
 * *path_out (caller vstr_clear). */
bool mp_wasm_find_pack(const char *dotted_name, vstr_t *path_out);

mp_obj_t mp_wasm_path_obj(void);
void mp_wasm_path_append(const char *root);

/* Local path: µPy VFS. http(s) URI: io.fetch, copied onto the GC heap. */
bool mp_wasm_read_file(const char *path, uint8_t **out, size_t *out_len);

/* Find + read + loader_load + sys.modules face. Raises on failure. */
mp_obj_t mp_wasm_import_pack(const char *dotted_name);

/* Seat fill: baked/ROM bytes (firmware cake). Path becomes mem:<name>. */
void mp_wasm_register_local_bytes(const char *dotted_name, const uint8_t *bytes, size_t len);

void mp_wasm_store_handle_on_module(mp_obj_t mod, pm_wasmmod_registry_handle_t h);
bool mp_wasm_load_handle_from_module(mp_obj_t mod, pm_wasmmod_registry_handle_t *out);

/* loader_unload when handle attrs present; always scrub sys.modules face. */
void mp_wasm_unload_pack(const char *dotted_name);

#endif /* MICROPY_INCLUDED_WASMMOD_PORTS_FINDER_H */
