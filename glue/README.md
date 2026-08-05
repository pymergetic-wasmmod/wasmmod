# glue/ — mirrors `include/`

Implementations for the public API under [`../include/`](../include/).

**Rule:** `include/pm_upy/mem/gc.h` ↔ `glue/pm_upy/mem/gc.c`.

Product code must **not** `#include` from here. Glue may include private
wasmmod / MicroPython headers.

There is **no** `glue/guest/` tree. Guest→host imports are header-only in
`include/pm_*` behind `#if PM_WASMMOD_GUEST`. Host native registration runs
inside `pm_wasmmod_runtime_init()` → `mp_wasm_host_register` /
`mp_wasm_loader_register` / `mp_wasm_upy_catalog_register`.

## Wave 1 — real vs stub

**Real (µPy / wasmmod backed):**

| Area | Glue |
|------|------|
| `pm_wasmmod_*` | `glue/pm_wasmmod/**` |
| features / init / mem / obj / hal / nlr | matching `glue/pm_upy/**` |
| sched / handle_pending / loop step | `loop/sched.c`, `loop/step.c` |
| REPL event helpers | `loop/repl.c` (needs `MICROPY_HELPER_REPL`) |
| run / pyexec / parse_compile_execute | `exec/run.c`, `exec/pyexec.c` |
| embed_exec_str | `exec/embed.c` (init/deinit stay feature-fail on non-embed) |
| vfs open/stat | `vfs/vfs.c` (needs `MICROPY_VFS`) |
| `*_available` lib/util probes | `lib/probe.c` (honest `MICROPY_PY_*` / config) |

**Still stubs** (`stubs_feature.c` / embed gaps):

- `pm_upy_raw_code_*`, await/sleep/new_awaitable/resume
- `pm_upy_profile_settrace`
- `pm_upy_embed_init` / `deinit` / `embed_exec_mpy` (port-specific)

Lib surfaces advertise **availability only** — no fake SSL/asyncio engines.
