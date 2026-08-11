# `pymergetic.wasmmod.registry` — design

The one source of truth for native module identity + exports. `sys.modules`
stays the Python import face; this table is what backs every native lookup
underneath it — every call direction (H→G / G→H / G→G / H→H), every impl
language. See `SOURCETREE.md` for the file/face conventions this module
itself follows; this doc is the runtime design, not the tree shape.

## Storage

**Allocator-backed growable table** (`Vec<ModEntry>`), not a fixed-capacity
static array. Fine everywhere in this tree's engines: `mp` always has TLSF,
`upy`/`mpwm` always have `malloc`/`mmap`, by the time the registry runs —
see the allocator-rules discussion this doc assumes. A fixed cap would
either be wastefully large or eventually insufficient, since packs are
loaded at runtime, not known at compile time.

**`fqn` is an owned heap `String` per entry**, not `&'static str`. Required
regardless of allocator choice: a dynamically-loaded wasm/elf pack's own
name isn't a compile-time constant.

## Concurrency

Real locking, not decoration — access is expected under **multithreaded
*and* cooperative** scheduling (metal's own one-runner-per-CPU model
included), so a lock-free "it's basically single-threaded" shortcut isn't
safe here. Backed by `pymergetic.util.lock`'s `Mutex<T>` (== `SpinLock<T>`,
reactivated from the pre-rewrite version): pure spin, no OS blocking call
anywhere, so it works identically on bare-metal (nothing to block on) and
host. One `static TABLE: Mutex<Table>` guards the whole table — no
finer-grained per-entry locking yet; revisit only if contention is ever
real, not speculatively.

## Handles: index + generation

`pm_wasmmod_registry_handle_t { index: u32, generation: u32 }`, passed **by value**
everywhere, never a pointer. `unpublish` doesn't shift indices (every other
live handle's index has to keep meaning the same thing) — it marks the
slot dead and leaves it for the next `publish` to reuse, bumping
`generation` when it does. Every lookup through a handle
(`export_set`/anything handle-keyed) checks `generation` matches before
touching the slot, so a handle held past its module's `unpublish` is always
**detectably stale**, never a use-after-free or a silent alias onto
whatever got published into that slot next. Cost: one extra `u32` per
handle and per slot — cheap, worth it given how much this tree already
cares about unload being symmetric and correct.

## Resolution surface

- `pm_wasmmod_registry_publish` / `_unpublish` / `_has` / `_export_set` —
  lifecycle.
- `pm_wasmmod_registry_resolve_native(fqn, export_name) -> *mut c_void` —
  the one universal lookup every direction bottoms out at. `NULL` if not
  found, never partial/stale data.
- `pm_wasmmod_registry_connect_import(fqn, export_name, out_slot) -> bool`
  — the "soft connect" pattern: resolve once at load/link time, cache
  into a slot, reuse the slot afterward instead of re-resolving by name
  on every call.

## GC and the `obj` export kind

`pm_wasmmod_registry_export_kind_t::Obj` exists from v1 (not deferred) — this tree needs
upstream MicroPython compatibility, and a Python object handle is a real
export shape from day one, not an add-on.

The registry stays **GC-agnostic while being GC-compatible by
construction**: an `Obj`-kind export's `ptr` is an **opaque `void*`
token** — the registry never interprets it, only stores and hands it back.
It never links against MicroPython, never knows what `mp_obj_t` is. The
only GC-facing surface is one visitor hook:

```c
void pm_wasmmod_registry_gc_visit(void (*visit)(void *token, void *ctx), void *ctx);
```

Calls `visit(token, ctx)` once per live `Obj`-kind token, while the table's
lock is held. Whichever embedder eventually attaches a GC (`upy`, via
something in the shape of `MP_REGISTER_ROOT_POINTER`) supplies `visit` and
interprets `token` as its own `mp_obj_t`. This keeps `wasmmod` buildable
and testable standalone (per "wasmmod first, metal/upy attaches later")
while still giving the eventual embedder a correct, complete root set —
nothing about deferring the *attachment* required deferring the *shape*.

## What v1 deliberately doesn't do yet

- No per-entry fine-grained locking (single table-wide lock only).
- No thunk/trampoline generation — that's `pymergetic.wasmmod.thunk`'s job,
  built on top of `resolve_native`/`connect_import`, not inside the
  registry itself.
- No pack/container parsing — `pymergetic.wasmmod.pack.format.*` publishes
  into this table, the registry doesn't know wasm/elf/aot exist as file
  formats.
- No `MICROPY_WASM_MALLOC`/TLSF wiring for the table's own storage — it
  uses whatever global allocator the crate is built against; a
  wasmmod-standalone build and a metal build get this for free from the
  allocator-rules decision (`mp` → TLSF, `upy`/`mpwm` → `malloc`/`mmap`)
  without the registry needing to know which one it is.

## Decision log

| Date | Decision |
|------|----------|
| 2026-08-11 | Storage: allocator-backed growable `Vec`, owned heap `String` fqn keys — packs are runtime data, not compile-time constants |
| 2026-08-11 | Concurrency: real lock required (multithreaded + cooperative expected), backed by reactivated `pymergetic.util.lock` (`Mutex<T>` == `SpinLock<T>`, pure spin, no OS blocking call) |
| 2026-08-11 | Handles are index + generation, by value, never a pointer — stale-after-unpublish is a checked error, never a silent alias |
| 2026-08-11 | `Obj` export kind + GC support included in v1, not deferred — registry stays GC-agnostic (opaque `void*` token, one `pm_wasmmod_registry_gc_visit` callback hook) while being GC-*compatible* by construction, so `wasmmod` never links against MicroPython yet upstream attachment still gets a correct root set |
