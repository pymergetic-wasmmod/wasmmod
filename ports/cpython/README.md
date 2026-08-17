# ports/cpython — CPython host face

Same import→ready contract as µPy. Reuses `ports/common/` (boot/load/memcookie).

```text
ports/cpython/
  _wasmmod.c       # PyInit_wasmmod → pymergetic.wasmmod
  finder.c         # pack path: POSIX + HTTP io + artifact CDN
  packbind.c       # MPWP .cpy./.py bind + ELF adapters
  objhandle.c      # Py_INCREF twin of µPy objhandle
  nativecall.c     # typed resolve dispatch
  hostready.c      # bind_py + attach export callables
  importhook.c     # builtins.__import__ wrap + pack-on-path + auto-ready
  modgen.c         # util.gen VfsSink POSIX ops + pyi provider
  pyexport.c       # CPython twin of wasmmod/pyexport/__impl__.c
  Makefile         # builds build/pymergetic/wasmmod*.so
```

## Build / smoke

```bash
make -C ports/cpython
make -C ports/cpython smoke
```

`PYTHONPATH=ports/cpython/build` — package shell extends `__path__` with
`PM_WASMMOD_HOST_SRC/pymergetic` so `import pymergetic.util.*` loads host leaves.

## Contract

| Step | What |
|------|------|
| **Import** | `import pymergetic.wasmmod` boots registry/loader + installs import hook |
| **Pack** | `wasm.path` / `sys.path` / artifact CDN → `import_pack` → `pack_bind` (wasm/aot/ELF) |
| **Ready** | `pymergetic.*` leaves (except package shells) → `pm_cpy_host_ready` |
| **Call** | attrs / `.call` / `.connect` use typed C ABI |
| **Gen** | `pymergetic.util.gen.run_vfs` / `.diff` (POSIX `VfsSink`); `.run` needs cargo `gen` |

**Not yet:** a kernel's strong `pm_wasmmod_host_io_*` (park). Stub is `ports/freestanding/`. Unix/CPython `https://` is the default io fill (mbedtls).
