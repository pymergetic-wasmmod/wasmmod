/* Wasm guest: micropython.runtime.ticks_ms (host catalog). */
#include "pm_upy/hal/time.h"

unsigned int elapsed(void) {
    return ticks_ms();
}
