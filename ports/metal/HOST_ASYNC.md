# Host-async / io_ops — generic port + Metal fill

Wasmmod OWNs the **API**. Hosts **fill** the engine. Apps do not branch.

## Generic shape (wasmmod)

| Piece | Role |
|-------|------|
| `mp_wasm_io_ops_t` (`io.h`) | `fetch` / `probe` / `yield` / `request` / `put` |
| `pm_upy_await` / `resume` / `sleep` | Dual-mode await face |
| Host registration | Mode A → natural µPy; Mode B → Metal park |

Port recipe: `ports/PORT.md`. Metal concrete files: `ports/metal/io_ops.*`,
`register_upy.*`, `ASYNC.md`.

## Mode fills

| Mode | Fill |
|------|------|
| **A** (MP / simulator) | libc/POSIX; natural asyncio; sim `io_ops` (HTTP/files) |
| **B** (Metal) | TLSF; runners; `upy_io_fill.c` HTTP park; `yield` → runner checkpoint |

## Metal status

| Hook | Status |
|------|--------|
| `fetch` / `probe` | Filled — HTTP park-to-completion |
| `yield` | Available for long I/O |
| `request` / `put` | Declared; treat as DECLINE until published |
| `register_upy` | Wired on embed boot |

## Simulator (Mode A)

Most packs should run without QEMU: `import_wasm`, CDN, await/sleep, scripting.
See Metal `docs/MP_SIMULATOR.md`. Real virtio / Dropbear / true SMP stay Metal-only.

Concrete fill: `ports/sim/io_ops.c` (`mp_wasm_sim_io_ops`) — same table shape as
Metal; unix may also use builtin `MICROPY_WASM_HTTP_NATIVE`. Smoke:
`packages/metal/scripts/dual_mode_async_smoke.py`.

## Do not

- Invent a Metal-only await API beside `pm_upy_*`
- Block forever inside io without `yield`/park on Mode B
- Put Metal product policy into the autonomous `wasmmod` PR branch
