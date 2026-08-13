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
- `pm_wasmmod_registry_call(fqn, export_name, args, nargs, results, nresults) -> int32_t`
  — resolve + call in one step against the `Value` convention below;
  the one path a cross-container `Fn` export is reached through.

## Call graph / portable pointers

Cross-language call edges, `pm_addr_t` / `pm_buf_t`, import faces, and the
Native vs Bridge split are documented in **`docs/CALLGRAPH.md`**. Scalar
`Value` convention below is the Bridge encoding only — public faces should
prefer typed Native calls or `pm_addr_t`, not naked wasm i32 offsets.

## Value convention

`resolve_native`/`export_set` never changed shape for this — they still
hand back/store a bare `void*`. What's new is a **documented contract**
every cross-container `Fn` export's `ptr` must conform to once it's
genuinely reached across a container boundary (today: the loader's
claimed wasm trampolines; later: elf/aot), so a caller on the other end
of `resolve_native` has something uniform to call through no matter
which impl language or container produced the export:

```c
typedef enum {
    PM_WASMMOD_REGISTRY_VALKIND_I32 = 0,
    PM_WASMMOD_REGISTRY_VALKIND_I64 = 1,
    PM_WASMMOD_REGISTRY_VALKIND_F32 = 2,
    PM_WASMMOD_REGISTRY_VALKIND_F64 = 3,
} pm_wasmmod_registry_valkind_t;

typedef struct {
    pm_wasmmod_registry_valkind_t kind;
    union { int32_t i32; int64_t i64; float f32; double f64; } of;
} pm_wasmmod_registry_value_t;

typedef int32_t (*pm_wasmmod_registry_fn_t)(const pm_wasmmod_registry_value_t *args, uint32_t nargs,
    pm_wasmmod_registry_value_t *results, uint32_t nresults);
```

Deliberately the same four primitive kinds as wasm's own core value
types — WAMR's own `wasm_val_t` uses this exact kind+union shape, so the
loader's trampolines build/read these directly against WAMR's call API
with no translation step at the one boundary (host↔wasm) where it would
actually cost something. Same-artifact native-to-native calls **never**
go through this: those stay a direct, really-typed function pointer via
`connect_import`, exactly as before — the `Value` convention only exists
for the genuinely cross-container/dynamic case.

`pm_wasmmod_registry_call(fqn, export_name, args, nargs, results, nresults) -> int32_t`
is resolve+call in one step against this convention: `-1` if the
module/export isn't found, otherwise whatever the resolved function
itself returns (by convention, `0` == success). This is the one call
path a `Fn` export claimed by the loader is ever reached through from
outside `wasmmod`.

## The loader: WAMR, and buffer/string marshaling

`pymergetic.wasmmod.loader` is the one module in this tree that knows
WAMR (`third_party/wamr`) exists — the registry itself never does, and
never will; every other module reaches wasm only through
`resolve_native`/`pm_wasmmod_registry_call`.

**Load path:** `wasm_runtime_get_file_package_type` (real detection —
sniffs the buffer's own magic number for `.wasm` vs `.aot`, so
`publish`'s container kind is never a hardcoded guess) → `wasm_runtime_load`
→ `wasm_runtime_instantiate` → `wasm_runtime_attach_shared_heap` →
`wasm_runtime_create_exec_env` → enumerate exports via
`wasm_runtime_get_export_count`/`get_export_type` straight from the
loaded module binary (no manifest duplication, same "source is truth"
rule as every other face in this tree) → for each `Fn` export,
`wasm_runtime_lookup_function` + claim one slot from a small fixed pool
of hand-written trampoline adapters + `pm_wasmmod_registry_publish`/
`_export_set`. `pm_wasmmod_loader_load` returns a
`pm_wasmmod_registry_handle_t` directly — a loaded module *is* one
registry entry, there's no separate "loader handle" to keep in sync
with it.

**AOT is a real, proven container kind, not just a reserved enum
value.** `wasm_runtime_load`/`instantiate`/`call_wasm_a` are the exact
same calls for both `.wasm` bytecode and `.aot` — WAMR dispatches
internally, so nothing else about the load path above branches on
which kind it is; only the `publish` call's container argument does.
Proven end-to-end against a real `.aot` file compiled at test-time by
`wamrc` (WAMR's own AOT compiler, built by `build.rs` against the
*system* LLVM — see `SOURCETREE.md`'s decision log), not a hand-rolled
byte array — unlike the `.wasm` fixture, AOT's compiled-native-code
format isn't something to hand-assemble byte-for-byte.

**Trampoline adapters.** A bare C function pointer can't close over
*which* wasm instance/function it should call — so instead of one
generic trampoline, the loader hand-writes a small fixed pool (8 today)
of distinct `extern "C" fn` addresses sharing one identical body
(convert incoming `Value`s to WAMR's `wasm_val_t`s, call
`wasm_runtime_call_wasm_a`, convert results back). "Claim a slot" means
handing out one of those addresses and recording which `exec_env`/
function it now means, in a slot-indexed side table — until `unload`
releases it back to the pool. What's genuinely per-export is the *slot
assignment*, not the logic; a macro generates the N addresses, it isn't
N different implementations.

**Buffer/string marshaling: WAMR shared heap**, created once at
`pm_wasmmod_loader_init` as a **runtime-managed** heap (not
`pre_allocated_addr` — WAMR refuses `shared_heap_malloc` on prealloc
heaps). Every loaded instance attaches the same heap; portable
`pm_wasmmod_loader_shared_alloc` / `_shared_free` / `_addr_to_native`
wrap those APIs for `pm_addr_t` / `pm_buf_t` (see `CALLGRAPH.md`).
`pymergetic.util.mem` remains available for other host arenas.

**Callable from any thread.** Every adapter call starts with an
unconditional `wasm_runtime_init_thread_env()` — `wasm_runtime_init()`
only sets up WAMR's hardware-bound-check signal env for the one OS
thread that called it, so a *different* thread later calling through
`pm_wasmmod_registry_call` would otherwise fail with `"thread signal
env not inited"`. This is a thread-local idempotent check on WAMR's own
side, not a real per-call re-init cost — found the hard way, via a
genuinely flaky (not one-off) `cargo test --lib` before this was added.

**Unload path:** `pm_wasmmod_registry_unpublish` (stale handles rejected
here, before touching anything WAMR-side) → release every adapter slot
the module's exports had claimed → `wasm_runtime_destroy_exec_env` →
`wasm_runtime_detach_shared_heap` → `wasm_runtime_deinstantiate` →
`wasm_runtime_unload`. The same teardown sequence is also the rollback
path for a `load()` that fails partway through — never a half-published
module left behind.

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
- No *generic*, engine-agnostic thunk/codegen story — that's still
  `pymergetic.wasmmod.thunk`'s eventual job, if/when a second engine
  needs one. What exists today (the loader's small fixed adapter pool)
  is WAMR-specific plumbing that belongs with the module that owns
  WAMR, not a preview of that generic story.
- No pack/container parsing beyond what the loader itself does for wasm
  — `pymergetic.wasmmod.pack.format.*` (elf/aot) still needs to publish
  into this table the same way; the registry doesn't know any container
  format exists, wasm included.
- No ELF container support in the loader yet. AOT *is* supported
  (interpreter + AOT both on in `vmlib`, real container-kind detection
  at load time, proven against a `wamrc`-compiled fixture) — see the
  decision log.
- No fast-jit. Not a scope choice — WAMR's own build system rejects
  `WAMR_BUILD_SHARED_HEAP=1` + `WAMR_BUILD_FAST_JIT=1` together, and
  shared heap is load-bearing for this loader. Root cause (see
  `SOURCETREE.md`'s decision log): fast-jit's own codegen
  (`core/iwasm/fast-jit/fe/jit_emit_memory.c`) never learned the
  shared-heap address-translation rule that both interpreters and AOT's
  LLVM-IR codegen have — a real upstream gap, not fixable from our side
  without patching vendored WAMR's own JIT backend.
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
| 2026-08-11 | Added the `Value`/`pm_wasmmod_registry_fn_t` convention + `pm_wasmmod_registry_call` resolve+call helper — reused WAMR's own `wasm_val_t` kind+union shape rather than inventing a parallel one, since the loader's trampolines build/read these directly against WAMR's call API |
| 2026-08-11 | `pymergetic.wasmmod.loader` added — WAMR-backed, first "wasm actually running" milestone: load → instantiate → attach shared heap → enumerate exports → claim adapter → publish, proven end-to-end against a hand-assembled `.wasm` fixture in the loader's own tests. See `SOURCETREE.md`'s decision log for the build-system/WAMR-flags/shared-heap-sizing details this session also settled |
| 2026-08-11 | AOT enabled (`WAMR_BUILD_AOT=1` in `vmlib`, real `wasm_runtime_get_file_package_type` detection replacing the loader's hardcoded `Wasm` container kind) and proven end-to-end against a real `wamrc`-compiled `.aot` file. Fast-jit was attempted in the same pass and dropped — WAMR 2.4.3 hard-rejects `SHARED_HEAP=1` + `FAST_JIT=1` at configure time; traced the root cause to fast-jit's codegen never having learned the shared-heap address-translation rule the interpreters and AOT's LLVM-IR codegen both have. See `SOURCETREE.md`'s decision log for the full build-system/investigation details |
