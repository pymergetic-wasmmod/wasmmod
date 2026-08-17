# ports/freestanding — wasmmod border for a host kernel

For images with no OS under them, where some host kernel owns the heap, the
sockets and the runners. µPy-facing APIs stay **sync**: the kernel parks async
work *inside* `io_ops`, so finder / CDN / pack load never see its sockets.

This directory is the last wasmmod-owned slice before that kernel's fill, and it
names no kernel: everything crosses as a weak `pm_wasmmod_host_io_*` hook. It is
**not** a TLS module, not `wasmmod.net.ip`, not `register_upy`.

## I/O ops

```c
#include "extmod/wasmmod/ports/freestanding/io_ops.h"

pm_wasmmod_host_io_ops_init();          /* before first use (UEFI PE) */
pm_wasmmod_io_set(&pm_wasmmod_host_io_ops);
/* or compile-time: #define MICROPY_WASM_IO_OPS pm_wasmmod_host_io_ops
 * (still call init before pm_wasmmod_host_boot / io_set(NULL)) */
```

The default stub **DECLINEs** every URI. The kernel provides strong:

| Weak hook | Wait | Fill |
|-----------|------|------|
| `pm_wasmmod_host_io_fetch` | async | HTTP GET park-to-completion |
| `pm_wasmmod_host_io_probe` | async | HTTP HEAD / exists |
| `pm_wasmmod_host_io_request` | async | POST/PUT publish |
| `pm_wasmmod_host_io_yield` | facade | runner checkpoint |

Always **call** the weak symbols (do not compare addresses to NULL).
`*out_bytes` comes from `MICROPY_WASM_MALLOC` → `pm_util_mem_alloc` on the
kernel's arena.

Unix/CPython keep the POSIX+mbedtls default fill. A freestanding image does not
compile that path — `io_ops` wins, and a DECLINE there would fall through to
POSIX sockets the image does not have.

## WAMR engine OWN

Freestanding interp recipe: `wamr_freestanding.mk`.

| Piece | Owner |
|-------|--------|
| Source list + `-D` / target flags | this mk |
| WAMR tree | `third_party/wamr` only |
| Platform glue (`platform_internal.h`, TLSF, ticks) | the host kernel |
| Final link of `libwasmmod_wamr_freestanding.a` | the host kernel |

Unix host WAMR stays cargo `build.rs` (cmake `vmlib`, AOT + shared heap).
This mk is interp + **shared heap** (rewrite loader) + AOT/JIT off.

```bash
make -f ports/freestanding/wamr_freestanding.mk \
  OUT_DIR=/tmp/wamr-fs \
  PLAT_INC=... PLAT_PORT_INC=... \
  PLAT_LIBC_INC=... PLAT_SRC_INC=... PLAT_EXTRA_INC=... \
  UEFI=0
```

Do not add `io_ops.c` to `micropython.mk` `SRC_WASMMOD`, CPython `CORE_SRCS`,
or cargo `build.rs` — the kernel's own mk compiles it, alongside its
`PLAT_BH_PLATFORM` glue.
