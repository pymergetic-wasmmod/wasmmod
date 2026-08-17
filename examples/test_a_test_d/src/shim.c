/* test_a.test_d native: C + Rust. */

int d_rs_value(void);

int d_value(void) {
    return d_rs_value() + 30; /* 37 */
}

int mp_pack_load(void) {
    return 0;
}

int mp_pack_unload(void) {
    return 0;
}
