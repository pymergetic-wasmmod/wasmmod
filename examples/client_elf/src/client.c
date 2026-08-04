/*
 * ELF client pack: undef `hello` resolved via wasmmod.imports + registry.
 */
int hello(void);

int use_hello(void) {
    return hello();
}

int mp_pack_load(void) {
    return 0;
}

int mp_pack_unload(void) {
    return 0;
}
