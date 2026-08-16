# ports/metal — wasmmod border for Metal

µPy-facing APIs stay **sync**. Metal parks async work *inside* `io_ops` so
finder / CDN / pack load never see Metal sockets or runners.

This directory is the last wasmmod-owned slice before Metal fill. It is
**not** a TLS module, not `wasmmod.net.ip`, not `register_upy`.

## I/O ops

```c
#include "extmod/wasmmod/ports/metal/io_ops.h"

pm_wasmmod_metal_io_ops_init();          /* before first use (UEFI PE) */
pm_wasmmod_io_set(&pm_wasmmod_metal_io_ops);
/* or compile-time: #define MICROPY_WASM_IO_OPS pm_wasmmod_metal_io_ops
 * (still call init before pm_wasmmod_host_boot / io_set(NULL)) */
```

Default stub **DECLINEs** every URI. Metal provides strong:

| Weak hook | Wait | Fill |
|-----------|------|------|
| `pm_metal_wasm_io_fetch` | async | HTTP GET park-to-completion |
| `pm_metal_wasm_io_probe` | async | HTTP HEAD / exists |
| `pm_metal_wasm_io_request` | async | POST/PUT publish |
| `pm_metal_async_yield` | facade | runner checkpoint |

Always **call** the weak symbols (do not compare addresses to NULL).
`*out_bytes` via `MICROPY_WASM_MALLOC` → `pm_util_mem_alloc` on Metal’s arena.

Unix/CPython keep the POSIX+mbedtls default fill. Metal does not compile
that path — `io_ops` wins, then DECLINE would hit POSIX sockets which
Metal does not have.

## WAMR engine OWN

Freestanding interp recipe: `wamr_freestanding.mk`.

| Piece | Owner |
|-------|--------|
| Source list + `-D` / target flags | this mk |
| WAMR tree | `third_party/wamr` only |
| Firmware platform glue | Metal `extmod/metal/port/wamr` |
| emcc platform glue | `ports/webassembly/wamr` (upywm has no metal) |
| Final link of `libwasmmod_wamr_freestanding.a` | firmware: Metal; browser: `micropython.mk` |

Unix host WAMR stays cargo `build.rs` (cmake `vmlib`, AOT + shared heap).
This mk is interp + **shared heap** (rewrite loader) + AOT/JIT off.

```bash
make -f ports/metal/wamr_freestanding.mk \
  OUT_DIR=/tmp/wamr-fs \
  METAL_PLAT_INC=... METAL_PORT_INC=... \
  METAL_LIBC_INC=... METAL_SRC_INC=... METAL_INCLUDE_INC=... \
  UEFI=0
```

Do not add `io_ops.c` to `micropython.mk` `SRC_WASMMOD`, CPython `CORE_SRCS`,
or cargo `build.rs` — Metal compiles it (`extmod/metal/metal.mk`).
