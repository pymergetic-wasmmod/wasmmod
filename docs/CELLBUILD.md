# Cell / chromosome build doctrine

**Status:** sinks, included-bytes diff, rich live `__init__.pyi`, µPy VFS glue,
`PM_MOD_EXPORT_C` / `PM_MOD_EXPORT_RS!` → one registry (2026-08-13).
**JIT (C/RS compilers in-bin):** later-later — do **not** block facegen/VFS/pyi
work on it; leave hooks open, no design debt that forbids JIT later.
**Owns:** why `pymergetic.util.gen` and in-bin build machinery exist.
**Does not own:** pack format bits, WAMR engine details (see REGISTRY / CALLGRAPH / SOURCETREE).

## One picture

The product is not only a *runtime that loads modules*. Long-term it is a
**cell**: it carries its own genetic material (embedded source), can
**unroll** that material, **rebuild faces/headers**, and **recompile /
re-JIT** modules against itself **while fully running**.

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
  C / RS JIT (later-later) + reload / soft-connect
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
| **Vfs** | `VfsSink` + ops table | `pm_util_gen_run_vfs` / `.run_vfs` | in-bin / browser VFS (µPy ops in `modwasmmod.c`) |
| **Mem** | `MemSink` | `pm_util_gen_run_mem` | compare, pack without a path |

**Diff:** `diff_against_included` / `.diff(fqn, h, rs, pyi)`.

**Rich `__init__.pyi`:** `pm_util_gen_set_py_face_provider` — µPy port imports
the fqn, walks callables, fills pyi; CLI without µPy keeps the stub.

**Autogen `__exports__.h` / `__exports__.rs`:** one emit pipeline
(`gen_fqn_to_sink`) after the fqn is in the live registry:

| Source | How it enters the registry for CLI gen |
|--------|----------------------------------------|
| `impl = "c"` / `"rs"` linked in-bin | `PM_MOD_EXPORT_C` / `PM_MOD_EXPORT_RS!` ctors |
| `impl = "py"` | host `ast` on `__init__.py` hints → `register_fn` (C/RS **access** faces; live µPy thunks later) |
| C card not linked (e.g. guest `hello`) | scan `PM_MOD_EXPORT_C(...)` in `__impl__.c` → `register_fn` |

Skip only when there are **no** hints and **no** macros (empty umbrella).
Hand-editing owned faces is a bug — regenerate with `wasmmod-gen`.

## Feature gate / menuconfig

| Knob | Where | Meaning |
|------|-------|---------|
| `MICROPY_PY_WASM` | make | enable wasmmod port |
| `MICROPY_PY_WASM_GEN` | make + `mpconfig_wasm.h` | util.gen face + cargo `gen` (unix default **1**) |
| Cargo `gen` / `build-machinery` | `Cargo.toml` | std FS card walk + CLI |

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
| **Pack** | Guest `__tests__.c` compiled into the chromosome; `wasmmod.tests` (MPTE) lists cases; loader registers trampolines (not product faces). `__tests__.*` also embed in `wasmmod.source` when source embed is on |
| **In-bin** | `wasmmod.test(fqn[, case])`, `wasmmod.test_all()`, `wasmmod.tests(fqn)`, `wasmmod.test_count(fqn)` |

## Next (non-JIT)

1. Keep JIT hooks open; do not couple facegen to a compiler.
2. Live `pm_wasmmod_pyexport_*` thunks so py access symbols actually call µPy
   (headers already emit; runtime still facegen-stub).

## Anti-patterns

- “Facegen is host-only; browser will never need it.”
- Treating `impl = "py"` as “no C/RS faces” (access headers are mandatory).
- Shipping a parallel host-only SoT that bypasses the registry emit path.
- Hand-editing `__exports__.h` that util.gen owns (incl. registry).
- Blocking VFS/pyi/cell work on in-bin C/RS JIT.
- Blobs of `#[cfg(test)]` inside `__impl__` — use `__tests__.*`.
- Publishing guest test symbols as product faces (loader must route via `wasmmod.tests`).