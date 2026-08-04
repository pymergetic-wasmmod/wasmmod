/* test_a2.test_b2.test_c2 — nested leaf; imports sibling d2. */

#include "../../../../../../guest.h"

MP_WASM_IMPORT("test_a2.test_d2", int, d2_value, void);

int c2_rs_tag(void);

int c2_answer(void) {
    return d2_value() + c2_rs_tag(); /* 37 + 5 = 42 */
}

int mp_pack_load(void) {
    return 0;
}

int mp_pack_unload(void) {
    return 0;
}
