# ports/metal — Metal host I/O for wasmmod

µPy-facing APIs stay **sync**. Metal parks async work *inside* these ops so
finder/load never see Metal sockets or runners.

## Wire-up

```c
#include "extmod/wasmmod/ports/metal/io_ops.h"
/* or after submodule path: wasmmod/ports/metal/io_ops.h */

mp_wasm_io_set(&mp_wasm_metal_io_ops);  /* runtime */
/* or compile-time: #define MICROPY_WASM_IO_OPS mp_wasm_metal_io_ops */

#include "extmod/wasmmod/ports/metal/register_upy.h"
mp_wasm_metal_register_upy();  /* after pm_upy_init — hooks Metal py edge */
```

Default stub **DECLINEs** every URI unless Metal provides strong
`pm_metal_wasm_io_fetch` / `pm_metal_wasm_io_probe` (weak hooks in `io_ops.c`).
Metal fills those with HTTP park-to-completion (`upy_io_fill.c`). See
[`../PORT.md`](../PORT.md), `ALLOC.md`, `ASYNC.md`, `mpconfig_metal.h`, `PACK.md`.

**Compatibility:** do not change `pm_*` or pack semantics here — only io ops +
optional resume registration.

## WAMR engine OWN

Freestanding engine recipe: `wamr_freestanding.mk` (see `WAMR.md`).
Metal compiles platform GLUE and links `libwasmmod_wamr_freestanding.a`.
Unix host WAMR: `ports/micropython/micropython.mk`.
