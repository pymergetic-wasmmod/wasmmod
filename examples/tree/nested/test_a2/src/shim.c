/* test_a2 native (package root of nested tree). */

int a2_rs_ping(void);

int a2_ping(void) {
    return a2_rs_ping() + 20; /* 21 */
}

int mp_pack_load(void) {
    return 0;
}

int mp_pack_unload(void) {
    return 0;
}
