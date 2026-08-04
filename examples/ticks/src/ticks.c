/* Wasm guest: micropython.runtime.ticks_ms (host catalog). */
#include "../../guest.h"

MP_WASM_IMPORT("micropython.runtime", unsigned int, ticks_ms, void);

unsigned int elapsed(void) {
    return ticks_ms();
}
