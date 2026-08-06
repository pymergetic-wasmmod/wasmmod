# Metal alloc / GC notes (upy-compatible)

Metal: **one TLSF**, upy GC **off** (`MICROPY_ENABLE_GC=0`), scheduler often
DEAD — py await = Metal async (`_async_bridge`).

## Compile-time (port)

```c
#define MICROPY_ENABLE_GC           (0)
#define MICROPY_ENABLE_SCHEDULER    (0)
/* Route instance/fetch buffers through Metal heap: */
#define MICROPY_WASM_MALLOC(sz)     pm_metal_mem_alloc(sz)
#define MICROPY_WASM_REALLOC(p, sz) pm_metal_mem_realloc(p, sz)
#define MICROPY_WASM_FREE(p)        pm_metal_mem_free(p)
```

`pm_upy_gc_enabled()` / `pm_upy_features()` already reflect `MICROPY_ENABLE_GC`
and scheduler flags — no fake GC engine in wasmmod.

## I/O

See `io_ops.c` — park Metal async inside sync `fetch`/`probe`; optional `yield`
for runner checkpoints. Keep signatures sync for µPy compliance.
