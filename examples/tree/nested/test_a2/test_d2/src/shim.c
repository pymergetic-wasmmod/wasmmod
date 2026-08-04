/* test_a2.test_d2 native. */

int d2_rs_value(void);

int d2_value(void) {
    return d2_rs_value() + 30; /* 37 */
}

int mp_pack_load(void) {
    return 0;
}

int mp_pack_unload(void) {
    return 0;
}
