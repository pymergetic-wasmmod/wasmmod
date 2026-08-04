/*
 * ELF guest → Python via wasmmod.host / wasmmod (System V natives, no WAMR).
 * Pointer args replace Wasm linear-memory offsets.
 */
int call0_i32(int slot);
int call_i32(int slot, int arg);
int call_buf(int slot, const void *ptr, int len);
int call_mem(int slot, int cookie);
int mem_alloc(int size);
int mem_copy_in(int cookie, const void *src, int n);
void mem_free(int cookie);
int call_py(const void *mod, int mod_len, const void *attr, int attr_len, int arg);
int mode(void);
int version(void *buf, int maxlen);

int via_host0(void) {
    return call0_i32(0);
}

int via_host(int x) {
    return call_i32(0, x);
}

int host_mode(void) {
    return mode();
}

int via_buf(void) {
    static const char msg[] = "elf";
    return call_buf(0, msg, 3);
}

int via_mem(void) {
    static const char msg[] = "cookie";
    int c = mem_alloc(6);
    if (c <= 0) {
        return -1;
    }
    if (mem_copy_in(c, msg, 6) != 0) {
        mem_free(c);
        return -1;
    }
    int r = call_mem(0, c);
    mem_free(c);
    return r;
}

int via_py(int x) {
    /* Host sets hello._elf_abs before calling (pack Python module is writable). */
    static const char mod[] = "hello";
    static const char attr[] = "_elf_abs";
    return call_py(mod, (int)sizeof(mod) - 1, attr, (int)sizeof(attr) - 1, x);
}

int host_version_len(void) {
    char buf[64];
    return version(buf, (int)sizeof(buf));
}
