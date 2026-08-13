# Call graph — portable edges (C / Rust / Python)

One module graph (FQN → registry entry ↔ `sys.modules` face). Call *edges*
are classified by ABI, not by source language.

See also: `REGISTRY.md` (storage / `Value` / loader trampolines),
`SOURCETREE.md` (`__imports__.*` / `__exports__.*` faces).

## Edge classes

| Class | When | What the caller holds |
|-------|------|------------------------|
| **Native** | ELF, resident, unisolated shared VA | Really-typed `fn` pointer (`connect_import` / `resolve_native`) |
| **Bridge** | Isolated wasm / AOT | `pm_wasmmod_registry_fn_t` (Values ↔ WAMR) — **one hop max** |

Same-artifact private calls stay typed and never enter the registry
(see SOURCETREE “Same-artifact calls stay private”).

```
resolve / connect_import
        │
   ┌────┴────┐
Native     Bridge
typed fn   Values + pm_addr translate
```

## Portable pointers (`pm_addr_t` / `pm_buf_t`)

Defined in `registry/__types__.h`:

| `space` | `off` meaning |
|---------|----------------|
| `PM_ADDR_SPACE_NATIVE` (0) | Host VA |
| `PM_ADDR_SPACE_SHARED` (1) | Shared-heap app address (WAMR) |
| `PM_ADDR_SPACE_MODULE` (2) | Module linear-memory app address |

Public faces (Py export, guest `__imports__`) pass **`pm_addr_t` / `pm_buf_t` /
scalars** — never naked wasm i32 offsets. Only loader adapters widen/narrow.

Translate / allocate (loader owns WAMR):

- `pm_wasmmod_loader_addr_to_native(handle, addr) → void *`
- `pm_wasmmod_loader_native_to_addr(handle, space, ptr) → pm_addr_t`
- `pm_wasmmod_loader_shared_alloc(handle, len, &buf)` / `_shared_free`

## Import faces (module is module)

| Lang | Import surface |
|------|----------------|
| **Python** | `import pymergetic….hello` |
| **C** | `#include "…/hello/__imports__.h"` (or peer `__exports__.h`) + `PM_MOD_CONNECT` |
| **Rust** | `mod` / `use` of `__imports__.rs` (hand or later codegen) |

Same FQN string in all three. Until PMM codegen exists, `__imports__.*` /
`__exports__.*` are **hand-written** to match the end-state shape; macros in
`guest` (`PM_MOD_EXPORT_C` / `PM_MOD_EXPORT_RS!`, `PM_MOD_CONNECT`) are markers + soft-connect
helpers, not a second ABI.

## Convenience API (`pymergetic.wasmmod.api`)

Thin helpers over `registry_call` / `connect_import` for scalar demos and
hand-written faces (`pm_wasmmod_api_call0_i32`, `call2_i32`, `connect`).
Not a parallel registry — just less boilerplate before codegen.

## Discipline (no codegen yet)

1. One bridge hop per call — no stacked adapters.
2. Prefer `connect_import` at load; `registry_call` for dynamic FQNs.
3. Host µPy: prefer per-export fun objects (or `api` helpers) over teaching
   callers raw i32 `w.call` lists for new code.
4. When unisolated / ELF lands, more edges become Native; faces stay the same.
