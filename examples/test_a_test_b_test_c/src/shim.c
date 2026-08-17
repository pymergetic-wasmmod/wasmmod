/* test_a.test_b.test_c: imports d_value from sibling test_a.test_d. */

#include "pymergetic/wasmmod/guest.h"

MP_WASM_IMPORT("pymergetic.wasmmod_examples.test_a.test_d", int, d_value, void);

int c_rs_tag(void);

int c_answer(void) {
    return d_value() + c_rs_tag(); /* 37 + 5 = 42 */
}

int mp_pack_load(void) {
    return 0;
}

int mp_pack_unload(void) {
    return 0;
}
