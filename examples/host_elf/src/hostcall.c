/*
 * ELF guest → Python via named __pm_modules imports / wasmmod (System V
 * natives, no WAMR). Pointer args are real host addresses (no linear-memory
 * offset translation needed for ELF, unlike a WAMR wasm guest).
 *
 * Each host callback below is its own plain (module, func) import, resolved
 * once when this pack is loaded — see examples/run_elf.py, which registers
 * every one of these with wasm.export_py* / wasm.export_py_bufptr/mem before
 * loading this pack (no slot number, no runtime name string, no rebinding
 * after load).
 */
int host_const(void);
int host_double(int x);
int host_len_buf(const void *ptr, int len);
int host_len_mem(int cookie);
int mem_alloc(int size);
int mem_copy_in(int cookie, const void *src, int n);
void mem_free(int cookie);
int _elf_abs(int x);
int mode(void);
int version(void *buf, int maxlen);

int via_host0(void) {
    return host_const();
}

int via_host(int x) {
    return host_double(x);
}

int host_mode(void) {
    return mode();
}

int via_buf(void) {
    static const char msg[] = "elf";
    return host_len_buf(msg, 3);
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
    int r = host_len_mem(c);
    mem_free(c);
    return r;
}

int via_py(int x) {
    /* Host self-exports hello._elf_abs (wasm.export_py) before this pack
     * loads — see examples/run_elf.py. */
    return _elf_abs(x);
}

int host_version_len(void) {
    char buf[64];
    return version(buf, (int)sizeof(buf));
}
