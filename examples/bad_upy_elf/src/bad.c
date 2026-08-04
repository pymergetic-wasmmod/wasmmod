/* ELF guest that imports micropython.runtime.sched — must fail at load. */
int sched(void);

int boom(void) {
    return sched();
}
