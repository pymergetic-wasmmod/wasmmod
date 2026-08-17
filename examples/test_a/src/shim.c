/* test_a native: C + Rust. */

int a_rs_ping(void);

int a_ping(void) {
    return a_rs_ping() + 10; /* 11 */
}

int mp_pack_load(void) {
    return 0;
}

int mp_pack_unload(void) {
    return 0;
}
