# ports/common — host glue without a Python runtime

C ABI used by **µPy** (`ports/micropython/`) and **CPython** (`ports/cpython/`).

| File | Role |
|------|------|
| `boot.c` | `pm_wasmmod_host_boot` — registry + default `io_set` (skipped if already set) + loader + kernel version + trust session; weak `pm_wasmmod_host_kernel_ready`; `presence_publish` |
| `load.c` | `pm_wasmmod_host_prepare` — MPZL unwrap + verify + kind; `pm_wasmmod_host_load_wasm` — WAMR loader path |
| `memcookie.c` | Borrowed `[ptr,len)` cookies for pyexport `mem` faces |

**Not here:** `sys.modules` / `PyModule` binding, import hooks, ELF adapter pools (`packbind`), util.gen VFS, objhandle (per-runtime GC / INCREF).

CPython: `ports/cpython/Makefile` + `make smoke`. µPy: `MICROPY_PY_WASM=1` unix build.
