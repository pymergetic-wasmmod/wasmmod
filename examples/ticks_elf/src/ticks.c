/* ELF guest: micropython.runtime.ticks_ms (catalog) must resolve at load. */
unsigned int ticks_ms(void);

unsigned int elapsed(void) {
    return ticks_ms();
}
