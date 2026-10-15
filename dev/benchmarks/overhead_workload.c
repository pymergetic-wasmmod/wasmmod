/* One workload translation unit, compiled separately for native and wasm32.
 * Unsigned arithmetic is defined modulo 2^32; no memory or SIMD workload. */
#include <stdint.h>

uint32_t overhead_mix(uint32_t state, uint32_t work) {
    for (uint32_t i = 0; i < work; ++i) {
        state ^= state >> 16;
        state *= UINT32_C(0x7feb352d);
        state ^= state >> 15;
        state *= UINT32_C(0x846ca68b);
        state ^= state >> 16;
    }
    return state;
}

#ifndef __wasm__
/* Separate translation unit and no LTO prevent pointer devirtualization in
 * the harness, without a volatile-pointer load in every timed call. */
typedef uint32_t (*overhead_fn)(uint32_t, uint32_t);
overhead_fn overhead_native_pointer(void) { return overhead_mix; }
#endif
