/*
 * ELF guest → Python via wasmmod.host (System V natives, no WAMR exec_env).
 */
int call0_i32(int slot);
int call_i32(int slot, int arg);
int mode(void);

int via_host0(void) {
    return call0_i32(0);
}

int via_host(int x) {
    return call_i32(0, x);
}

int host_mode(void) {
    return mode();
}
