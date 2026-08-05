# Public C/C++/Rust compile surface

**Alpha.** This directory is the **only** product header surface for hosts and guests.
Do not `#include` private `extmod/wasmmod/*.h` impl headers from product code.

**Rust:** bindgen crate lives at [`../crates/pm`](../crates/pm) (`cargo check -p pm`).
There are no Rust sources under `include/`.

## Umbrellas

| Header | Role |
|--------|------|
| [`pm_wasmmod.h`](pm_wasmmod.h) | Pack load/call, CDN/path, host slots/cookies, module face |
| [`pm_upy.h`](pm_upy.h) | µPy gut control (GC, loop, exec, obj, …) — A+B+C keywords |
| [`pm_guest.h`](pm_guest.h) | `PM_WASMMOD_GUEST` + `MP_WASM_IMPORT*` macros |

Fine-grained: `#include <pm_upy/mem/gc.h>`, `#include <pm_upy/hal/time.h>`, …

Guest→host imports live in the same `pm_*` headers behind `#if PM_WASMMOD_GUEST`.
There is **no** `include/guest/` redirect tree — include the real headers.

## Guest vs host

| Build | `PM_WASMMOD_GUEST` | Import attrs |
|-------|-------------------|--------------|
| Host (µPy / embedder) | 0 (default) | off |
| Wasm / AOT guest (`__wasm__*`) | auto 1 | `import_module` / `import_name` |
| ELF ET_REL guest | set `-DPM_WASMMOD_GUEST=1` | plain `extern` (loader resolves) |

Example guest: `#include <pm_upy/hal/time.h>` then call `ticks_ms()` (catalog import). Host calls `pm_upy_ticks_ms()`.
`wasmmod pack` / example Makefiles add `-I$(WASMMOD_ROOT)/include`.

## Prefixes

- `pm_wasmmod_*` — wasmmod product API (host)
- `pm_upy_*` — MicroPython host-control + feature query
- Guest **import modules / field names** stay `wasmmod.host` / `micropython.runtime` (short C names under `PM_WASMMOD_GUEST`)

## Features

Build config (`MICROPY_*` / menuconfig) decides what is live. Metal may mark GC/scheduler DEAD.

- Compile-time: `PM_UPY_HAS_*` (see [`pm_upy/features.h`](pm_upy/features.h))
- Runtime: `pm_upy_features()` / `pm_upy_has()` (guest: same header → `features` / `has` imports)

Unavailable call: Python raise / C `PM_ERR_FEATURE` / Rust `Err`.

## Call matrix

| Direction | Entry |
|-----------|--------|
| Host → guest | `pm_wasmmod_pack_load*` / `pm_wasmmod_pack_call_*` |
| Guest → host | same `pm_*` headers with `PM_WASMMOD_GUEST` imports |
| Guest → guest | pack call / loader natives |

## wasmmod as module face

`pm_wasmmod_module_install("wasm")` at runtime init via `pm_upy_module_install_face`.

## Glue

Implementations live in [`../glue/`](../glue/) mirroring these paths.
