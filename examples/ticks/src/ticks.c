/* Wasm guest: micropython.runtime.ticks_ms (host catalog).
 *
 * The import module name is what the host catalog registers under, so the
 * loader resolves this from the wasm import section without the manifest
 * naming it. The ELF twin (../../ticks_elf) declares the same import in its
 * pack.toml, because an ET_REL object carries no import section to read. */
#include "pymergetic/wasmmod/guest.h"

MP_WASM_IMPORT("micropython.runtime", unsigned int, ticks_ms, void);

unsigned int elapsed(void) {
    return ticks_ms();
}
