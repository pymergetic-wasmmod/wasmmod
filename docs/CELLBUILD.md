# Cell / chromosome build doctrine

**Status:** sinks, included-bytes diff, rich live `__init__.pyi`, µPy VFS glue,
`PM_MOD_EXPORT_C` / `PM_MOD_EXPORT_RS!` → one registry; live `pm_wasmmod_pyexport_*`
thunks + `wasmmod.bind_py` (2026-08-13); host `io` table + metal-cdn client + µPy
`wasm.cdn` / `catalog` / `publish` (POST via `io.request`); default `https://` via
mbedtls in the io fill (2026-08-14); `ports/metal/` io_ops stub; CPython pack finder / `pack_bind` / ELF
+ util.gen VFS.
**Owns:** why `pymergetic.util.gen` and in-bin build machinery exist.
**Does not own:** pack format bits, WAMR engine details (see REGISTRY / CALLGRAPH / SOURCETREE).

## One picture

The product is not only a *runtime that loads modules*. Long-term it is a
**cell**: it carries its own genetic material (embedded source), can
**unroll** that material, **rebuild faces/headers**, and **reload /
soft-connect** modules against itself **while fully running**.

```text
  chromosome (pack / module)
       │  embed source (already shipping: keep_source / MPZL / …)
       ▼
  unroll → introspect live registry (+ µPy import)
       ▼
  util.gen emits faces → FsSink / VfsSink / MemSink
       ▼
  compare to included autogen  OR  omit autogen from bin and recreate
       ▼
  reload / soft-connect (+ bind_py for impl=py)
       ▼
  AI core (later) drives unload → rebuild → relink as normal ops
```

Wasm / native modules behave like **chromosomes**: when needed they expose
source, rebuild their ABI mirrors, and get rebuilt *in place*. That is why
facegen is **introspection inside the product**, not a host regex scan of
a checkout tree.

## Emit sinks

| Sink | Rust | C / µPy | When |
|------|------|---------|------|
| **Fs** | `FsSink` + `gen_run_path` | `pm_util_gen_run` / `.run` | unix / CI (`feature = "gen"`) |
| **Vfs** | `VfsSink` + ops table | `pm_util_gen_run_vfs` / `.run_vfs` | in-bin / browser VFS (µPy + CPython `modgen.c`) |
| **Mem** | `MemSink` | `pm_util_gen_run_mem` | compare, pack without a path |

**Diff:** `diff_against_included` / `.diff(fqn, h, rs, pyi)`.

**Rich `__init__.pyi`:** `pm_util_gen_set_py_face_provider` — µPy port imports
the fqn, walks callables, fills pyi; CLI without µPy keeps the stub.

**Autogen `__exports__.h` / `__exports__.rs`:** one emit pipeline
(`gen_fqn_to_sink`) after the fqn is in the live registry:

| Source | How it enters the registry for CLI gen |
|--------|----------------------------------------|
| `impl = "c"` / `"rs"` linked in-bin | `PM_MOD_EXPORT_C` / `PM_MOD_EXPORT_RS!` ctors |
| `impl = "py"` | host scan of `__init__.py` hints → `register_fn` (access faces); runtime `bind_py` installs live µPy thunks |
| C card not linked (e.g. guest `hello`) | scan `PM_MOD_EXPORT_C(...)` in `__impl__.c` → `register_fn` |

Skip only when there are **no** hints and **no** macros (empty umbrella).
Hand-editing owned faces is a bug — regenerate with `wasmmod-gen`.

## Feature gate / menuconfig

| Knob | Where | Meaning |
|------|-------|---------|
| `MICROPY_PY_WASM` | make | enable wasmmod port |
| `MICROPY_PY_WASM_GEN` | make + `mpconfig_wasm.h` | util.gen face + cargo `gen` (unix default **1**) |
| Cargo `gen` / `build-machinery` | `Cargo.toml` | std FS card walk + CLI |
| Cargo `bundle-mbedtls` | default on | compile µPy-vendored mbedtls into the rust lib (cargo test / CPython). µPy firmware: `--no-default-features` — unix already has mbedtls |

Defaults: `ports/micropython/mpconfig_wasm.h`.

## Versions (complete)

| Piece | Where |
|-------|--------|
| **Lib** | `pymergetic.util.version` — `cmp` / `satisfies` (`*`, exact, `>=`, `^`, PEP440 `XaN`) |
| **Registry** | `ModEntry.version` + `publish_ver` / `set_version` / `version` query (empty = unset) |
| **Pack bake** | Always-on `wasmmod.pkg` (MPPK); fallback `wasmmod.source` pkg_version |
| **Load** | loader + ELF publish bake pkg → registry |
| **Deps** | finder: non-`*` pin vs registry; missing registered version → ImportError |
| **Kernel** | card `version` → gen `__version__.h` → `MICROPY_WASM_VERSION` seed → registry; `wasmmod.version` reads registry |
| **Cards** | `deps` = `build` roots only; `version` = roots **or** host/kernel publish units |

## Module tests (`__tests__.*`)

| Piece | Where |
|-------|--------|
| **Layout** | `__tests__.rs` / `.c` / `.py` beside the card; optional `__tests__/` case split (not a fqn) |
| **Register** | `PM_MOD_TEST_RS!` / `PM_MOD_TEST_C` → `ModEntry.tests` (parallel to exports) |
| **ABI** | `fn() -> i32` — 0 pass, nonzero fail |
| **Host** | `cargo test` + `util::mod_test::registry_mod_tests_all` (`WASMMOD_TEST_FQN` filter) |
| **Pack** | Guest `__tests__.c` compiled into the chromosome; `wasmmod.tests` (MPTE) lists cases; loader registers pack tests as `WasmExport` names and runs them via the single wasm test runner hook (not product faces). `__tests__.*` also embed in `wasmmod.source` when source embed is on |
| **In-bin** | `import pymergetic.wasmmod` → `.test(fqn[, case])`, `.test_all()`, `.tests(fqn)`, `.test_count(fqn)` |

## Port layout

| Path | Role |
|------|------|
| `ports/common/` | C ABI boot/load/memcookie (µPy + CPython) |
| `ports/micropython/modwasmmod.c` | Thin module tables + wrappers |
| `ports/micropython/finder.c` | Pack path: VFS + HTTP `io` + metal-cdn `artifacts/` when configured |
| `ports/micropython/modutil.c` | Builtin `pymergetic.util` + `__path__` → host `src/` |
| `ports/micropython/importhook.c` | `__import__` wrap + `ensure_inited` + auto-ready |
| `ports/micropython/hostready.c` | Import → `bind_py` + attach typed export funobjs |
| `ports/micropython/nativecall.c` | Typed resolve dispatch (`wasmmod.call` / pack funobjs) |
| `ports/micropython/modgen.c` | `util.gen` / `wasmmod.gen` (GEN=1) |
| `ports/cpython/` | CPython twin: `_wasmmod` + finder/packbind/ELF + `modgen.c` VFS + objhandle + import→ready |
| `ports/metal/` | io_ops stub (weak DECLINE + yield + runtime init) + freestanding WAMR mk |
| `extmod/metal/` | `pymergetic.metal` cards; faces from `util.gen` (`PM_MOD_EXPORT_C`); heap is `pymergetic.util.mem` |

## Import → ready (no extrawurst)

| Step | What |
|------|------|
| **Import** | `import pymergetic.util.<leaf>` — util is builtin with `__path__` under `MICROPY_WASMMOD_HOST_SRC`; leaves load from host tree (py `__init__.py` or C/RS namespace dirs) |
| **Ready** | Import hook calls `mp_wasm_host_ready` / `pm_cpy_host_ready`: pyexport thunks for `impl=py`, attach missing export callables for C/RS |
| **Call** | Python attrs / `wasmmod.call` / CONNECT all use typed C ABI (not Value-convention) for known shapes |
| **`bind_py`** | Still available as explicit re-ready |

## `impl = "py"` live thunks

| Piece | Where |
|-------|--------|
| **Pools** | `pymergetic/wasmmod/pyexport/__impl__.c` (µPy) / `ports/cpython/pyexport.c` |
| **API** | `pm_wasmmod_pyexport_export_py*` / `pm_wasmmod_pyexport_bind_module` (`pm_wasmmod_py_obj_t`) |
| **Hosts** | Auto on import; optional `bind_py(fqn[, module])` |
| **Shapes** | 0..3×i32→i32, i64/f32/f64, bufptr, cookie **mem**, handle **obj** |
| **Host tables** | `ports/common/memcookie.c`; `ports/*/objhandle.c`; typedefs in `wasmmod/host/__types__.h` |
| **Hints** | `bytes`→bufptr; `"mem"`→cookie; `"obj"`→handle; `i64`/`int64`, `f32`/`float`, `f64`/`float64` (`util.gen` discover) |

## Next

Metal leaves on `pymergetic.metal`: `async` + `net.ip` + `net.tls` + `net.http`; faces from `util.gen`. Unix `METAL=1` boots. Heap stays `pymergetic.util.mem`. Next: RS ASGI, tap.

## Anti-patterns

- “Facegen is host-only; browser will never need it.”
- Treating `impl = "py"` as “no C/RS faces” (access headers are mandatory).
- Shipping a parallel host-only SoT that bypasses the registry emit path.
- Hand-editing `__exports__.h` that util.gen owns (incl. registry).
- Blobs of `#[cfg(test)]` inside `__impl__` — use `__tests__.*`.
- Publishing guest test symbols as product faces (loader must route via `wasmmod.tests`).
- Manual `bind_py` as the happy path (import should ready the leaf).