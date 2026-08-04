/*
 * Minimal freestanding ELF pack guest (ET_REL) for wasmmod in-tree loader.
 */
int hello(void) {
    return 42;
}

int add(int a, int b) {
    return a + b;
}
