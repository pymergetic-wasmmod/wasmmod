# Module tree (Py / C / RS)

**Status:** redesign in progress — decisions below are locked as we go.

Purpose: one import tree that behaves like normal Python packages, whether the
muscle is Python, C, or Rust.

---

## Locked

### Tree

- One tree under `src/`. **No parallel `include/`.**
- **Path == module:** `src/pymergetic/metal/net/ssh.*` → `pymergetic.metal.net.ssh`.

### Sample (`src/`)

```text
src/pymergetic/metal/
├── util/
│   ├── string_utils.py              # impl=py  →  pymergetic.metal.util.string_utils
│   └── string_utils.pmm.toml        # impl = "py"
│
├── microdot.pmm.toml                # impl = "py"  →  pymergetic.metal.microdot
├── microdot/
│   ├── __init__.py                  # package body
│   ├── request.py                   # leaf (add request.pmm.toml if its own module)
│   └── request.pmm.toml             # optional; only if request is a separate module
│
└── net/
    ├── ip.rs                        # impl=rs  →  pymergetic.metal.net.ip
    ├── ip.pmm.toml                  # impl = "rs"
    ├── ip.types.rs                  # types SoT (human; or live inside ip.rs)
    ├── ip.types.h                   # emitted (cbindgen)
    ├── ip.export.h                  # emitted
    ├── ip.import.h                  # emitted or hand
    ├── ip.h                         # optional umbrella
    │
    ├── ssh.c                        # impl=c   →  pymergetic.metal.net.ssh
    ├── ssh.pmm.toml                 # impl = "c"
    ├── ssh.types.h                  # types SoT (human)
    ├── ssh.export.h                 # human provide face (role-guarded)
    ├── ssh.import.h                 # human / connect needs
    ├── ssh.h                        # optional umbrella
    ├── ssh.types.rs                 # emitted (bindgen)
    ├── ssh.export.rs                # emitted
    ├── ssh.import.rs                # emitted or hand
    ├── ssh.rs                       # optional human barrel (reexport faces only)
    │
    └── ssh/                         # children of ssh (only when needed)
        ├── key.c
        ├── key.pmm.toml             # impl = "c"  →  pymergetic.metal.net.ssh.key
        ├── key.types.h
        ├── key.export.h
        ├── key.import.h
        ├── key.types.rs
        ├── key.export.rs
        └── key.import.rs
```

Legend: module node = muscle + `*.pmm.toml` (+ faces). Py packages: card beside the folder (`microdot.pmm.toml` + `microdot/`). No `include/` twin tree.

### Shape for `A.B.C`

| Lang | Module body | Children |
|------|-------------|----------|
| C | `A/B/C.h` pieces + `A/B/C.c` | `A/B/C/<child>…` |
| RS | `A/B/C.rs` | `A/B/C/<child>…` |
| Py | `A/B/C.py` **or** `A/B/C/__init__.py` | under `A/B/C/` |

C/RS: sibling file = module body (like `__init__`); folder = submodules only.

### One defining lang

- Exactly one of C / RS / Py **defines** the module.
- Other langs only **consume** via faces.

### Module card (once, forever)

Sibling file: **`A/B/C.pmm.toml`** (`pmm` = pymergetic module).

```toml
impl = "c"   # c | rs | py
```

- **Only required key today:** `impl`.
- Module **name is not stored** — derived from path under `src/`.
- More keys may appear later in the **same** file; no `*.pmm.impl` sidecars.

### No `pack.toml` — one manifest system

`pack.toml` is retired. Everything it did either dies or moves onto the
card of whichever node is a deliverable root:

| `pack.toml` had | Now |
|---|---|
| `type`, `comment`, `description`, `license` | **gone** — grepped every tool, nothing ever read these; pure decoration |
| `name` | gone — derived from path, same as every other module |
| `native.dir` / `native.sources` | gone — implicit: every muscle file under this node's subtree |
| `[[exports]]`, `[[imports]]` (incl. ELF's `sig`) | gone — see "Exports/imports come from faces" below |
| `lifecycle.load` / `.unload` | → card keys `on_load` / `on_unload` (any module, not just roots) |
| `[deps]` | → card key `deps` (table); only meaningful where `build` is set |
| `python.freeze` / `.targets` / `.keep_source`, `[source]`, `[pack]` | → nest under `[build]` on the root card only (see below) |

Optional card keys, all still just fields in the same one `*.pmm.toml`:

```toml
impl = "c"
on_load = "mp_pack_load"      # optional
on_unload = "mp_pack_unload"  # optional
deps = { "pymergetic.wasmmod_examples.hello" = "0.1.0" }  # optional
```

### Deliverable root

A node becomes a compiled deliverable purely by having a `build` key —
everything else about it (path, `impl`, faces) is identical to any other
module:

```toml
impl = "c"
build = ["wasm", "elf"]   # array: one source tree, N container twins
version = "0.1.0"         # only meaningful for a root (deps resolution)

[build]
freeze = true
targets = ["upy:mpy6:sib31", "cpy:cp312"]
keep_source = true
embed_source = true
compress_source = true
compress_pack = true
```

`build` being an array (not a string) matters in practice: `examples/ticks/`
and `examples/ticks_elf/` are two hand-duplicated directories today (own
`src/ticks.c`, own `pack.toml`, own build tool) producing `ticks.wasm` and
`ticks.elf` from what should be one module. Collapsing that to
`build = ["wasm", "elf"]` on one node needs the `*.import.h` face codegen to
branch per target (`MP_WASM_IMPORT(...)` under a wasm build, a bare `extern`
prototype under an ELF build — ELF has no import section, `elf_resolve_import`
resolves it directly) — same declared dependency, different low-level
plumbing. Not automatic; the face generator has to know which target it's
emitting for.

### Exports/imports come from faces, not lists

No `[[exports]]` / `[[imports]]` anywhere, for either container. A module's
`*.export.*` face already declares what it provides; its `*.import.*` face
already declares what it needs. The packer, for a deliverable root, unions
the export/import faces of every node in its subtree to get the WASM
`--export=` list / the `wasmmod.exports` section / ELF's import grouping —
no hand-maintained duplicate of information the faces already have. This
also closes the one case that survived the WASM auto-discovery work: ELF's
`[[imports]]` existed only because a compiled ELF object has no
module-namespace info — but the *source-level* import face always had it;
reading the face (or the tree of cards) instead of the binary removes the
last reason ELF needed manifest-level import declarations.

### Same-artifact calls stay private

Applies whenever two module nodes land in the **same compiled artifact** —
the native `mp` firmware binary, or one wasm/ELF pack bundling several nodes
(see "No flat-dump exception" below). That's the only place plain C/Rust
linkage could physically bypass `__pm_modules` — crossing a real pack/process
boundary already can't be bypassed this way, nothing changes there.

- The real implementation is **private**: `static` in C (no external
  linkage — no other translation unit can ever link it directly),
  non-`pub`/no `#[no_mangle]` in Rust.
- `*.export.h`/`*.export.rs` never declares a bare `extern` of the real
  symbol. It declares a **slot-backed inline wrapper** instead — same
  call-site shape regardless of what the callee turns out to be:

  ```c
  extern void *__pm_slot_ssh_connect;
  static inline int ssh_connect(int fd, const ssh_opts_t *opts) {
      return ((int (*)(int, const ssh_opts_t *))__pm_slot_ssh_connect)(fd, opts);
  }
  ```

- The slot is filled **eagerly at connect time** (same timing as everything
  else already built — `pm_mod_connect_import`/`pm_mod_connect_guest`), not
  lazily per call.
- Registration is **one macro invocation placed in the same file, right
  after the static definition** — same call-site idiom `PM_METAL_REG_MOD`
  already uses today, generator-inserted, never a separate `*.reg.c` (which
  would be a different translation unit and couldn't take a `static`
  function's address at all):

  ```c
  static int ssh_connect_impl(int fd, const ssh_opts_t *opts) { ... }
  PM_MOD_EXPORT_C(ssh, ssh_connect, ssh_connect_impl, int(int, const ssh_opts_t *));
  ```

  This answers the registration-codegen question above: no hand-written
  `pm_mod_export_set` calls, no sibling reg file — one generator-placed macro
  per export, same file as the impl.
- Consumer never knows or cares which case applies — the header shape is
  identical whether the callee ends up same-artifact-native, a wasm peer, or
  an ELF peer.

### No flat-dump exception — not even for test fixtures

Every module follows the tree/card/faces rules, full stop. A test fixture
that violates the target shape isn't neutral scaffolding — it normalizes
the exact drift this redesign exists to kill, and it means the fixture never
actually exercises the shape real modules are supposed to have.

`examples/bridge/` is the concrete case: today one flat `pack.toml` +
one flat `bridge.c` (~30 exports) + one flat `lib.rs` (~24 exports). Target
shape is several real child nodes bundled under one `build` root — grouped
by concern, not dumped by language:

```text
examples/bridge/src/pymergetic/wasmmod_examples/bridge/
├── __init__.py + .pmm.toml        impl=py  — self-export ping/ping_code
├── native.c + .pmm.toml           impl=c   — add3, via_i64/f32/f64, scale_add_f64, via_c_self
├── native_rs.rs + .pmm.toml       impl=rs  — rs_square, rs_add3, rs_scale_add_f64, rs_via_square
├── host.c + .pmm.toml             impl=c   — via_host*, via_buf, via_mem, via_handle, via_host_c/rs
├── host_rs.rs + .pmm.toml         impl=rs  — rs_via_host*, rs_via_buf/mem/handle, rs_via_host_c/rs
├── peer.c + .pmm.toml             impl=c   — via_hello, via_mixed*, via_loader_*, via_peer_*
├── peer_rs.rs + .pmm.toml         impl=rs  — rs_via_hello, rs_via_mixed_i64, rs_via_loader_hello, rs_via_peer_py
└── matrix.c + .pmm.toml           impl=c   — matrix, matrix_rich (cross-direction combos)
```

Bundling several distinct-`impl` child nodes under one `build` root is
exactly how "exactly one lang defines a module" scales up — it's not an
exception to that rule, it's the rule applied one level up.

**Blocked on tooling**, not a decision: doing this split for real needs the
`*.pmm.toml`-driven packer, face-based export/import discovery, and the
registration codegen above to exist first. Splitting the files by hand today
without that machinery would just be churn.

### Faces (types / export / import)

Per module, same triad in **every** `impl` — C, RS, **and Py**. Py is not a
second-class case: C/Rust must always be able to call Python, no exceptions,
so a `py` module's export face is generated exactly like a `c`/`rs` module's
would be.

| Slice | Role |
|-------|------|
| `*.types.*` | Shared ABI shapes |
| `*.export.*` | What this module **provides** (H→G / G→G / resident) |
| `*.import.*` | What this module **needs** (G→H / G→G soft edges) |

- **Role** (host vs guest): guard on the **export** face (`PM_WASMMOD_GUEST` or equivalent).
- **Edges:** export vs import are **split files** (not triple-ifdef directions).
- Optional umbrella `C.h` may recombine; consumers may include `.export` only.
- **Py has no import face** — plain `import`/attribute access is already
  location-transparent (that's what `sys.modules` is for). The triad's
  `import` slice only exists for `c`/`rs` modules, which have no equivalent
  built-in mechanism.

### Types SoT (not “always gen”)

| `impl` | Types SoT | Types mirror |
|--------|-----------|--------------|
| `c` | `*.types.h` (human) | `*.types.rs` (bindgen) |
| `rs` | human RS (`*.rs` / `*.types.rs`) | `*.types.h` (cbindgen) |
| `py` | **the function's own type hints** — parsed statically via `ast`, same posture as bindgen/cbindgen reading real source, not a manifest | `*.export.h` + `*.export.rs`, generated from the parsed hints, same as any other module |

`export` / `import` on the **foreign** side are normally emitted; defaults follow
`impl` (no faces table in `.pmm.toml` unless we need opt-out later).

### Py export face — parsed from type hints, not declared anywhere

No manifest field, no `sig` key on the card — that would just be the same
export-list-duplicates-the-source mistake `pack.toml` made, applied to
Python instead of C. The function's own type hints are the signature,
parsed statically via `ast` (never executed — same posture as bindgen/
cbindgen reading real source):

```python
# util/__init__.py — just the function; no self-export boilerplate
def ping_code() -> int:
    return 7
```

```toml
# util.pmm.toml — nothing export-specific at all
impl = "py"
```

Hint → `pm_mod_export_py*` shape mapping (reusing the exact taxonomy that
family already has):

| Hint | Shape |
|---|---|
| `int -> int` | `i32` (default) |
| `bytes -> int` | `mem` — matches `pm_mod_export_py_mem` exactly: native side gets an `int32` cookie, marshals to `bytes` *before* calling Python; the `bytes` annotation on the Python side already **is** that marshaling declaration |
| unannotated / arbitrary object param | `obj` (handle-resolved) |
| `pymergetic.wasmmod.types.i64` / `f32` / `f64` | disambiguates where Python's own `int`/`float` are too coarse — project-provided aliases, still 100% derived from source via `ast`, never a manifest |
| `pymergetic.wasmmod.types.RawBuf` | the ELF-only `bufptr` shape — explicit opt-in marker, never inferred, since it's a real host pointer and already flagged as unsafe for a wasm guest in `pyexport.h` |

The packer, parsing a `py` module's functions this way, generates **both**:

- `util.export.h` / `util.export.rs` — same slot-backed inline wrapper shape
  as a `c`/`rs` export (see "Same-artifact calls stay private" — nothing
  about being Python exempts a same-artifact call from going through a slot).
- the registration call itself (picks the right `pm_mod_export_py*` variant
  off the parsed hint) — replaces every hand-written `_wasm.export_py(...)`
  call that today lives inside `__init__.py` (see `bridge`/`hello.util`'s
  current source — that boilerplate goes away once this generates).

### Naming

- No `.gen.` infix — `types` / `export` / `import` already mark face files.
- Banner / tooling owns “do not edit” on emitted faces.
- C-defined: human muscle = `C.c`. Optional human `C.rs` = **barrel reexport** of faces only (not a second impl).

### Tooling (intent)

- **bindgen:** C → RS consume faces (`types` / `export`; `import` only if mechanical).
- **cbindgen:** RS → C consume faces.
- Soft-connect `.import` may use a small custom emitter or stay hand-written.
- Neither tool covers: cross-module `.import.*` wiring (each only mirrors
  *your own* API, not someone else's), registration into `__pm_modules`,
  WASM-vs-ELF target branching for a `build` array, or anything touching
  Python faces (`wasm.export_py*`/`pm_mod_export_py`) — no generic tool
  understands MicroPython's `mp_obj_t` API. All of that stays bespoke.

### Crate boundaries follow `build`, not subsystem taste

`extmod/metal/` today: **28 Rust crates**, one Cargo workspace, 454 lines of
pure `Cargo.toml` boilerplate, 131 hand-maintained inter-crate dependency
edges — a third parallel manifest (the Cargo graph) duplicating what the
source tree and `*.import.rs` faces already know. 24 of those 28 crates have
**zero files** directly in their own `src/`; every one is a thin shell whose
`[lib] path` already redirects into the one shared tree (`fs_fat`'s only
points at `src/pymergetic/metal/fs/fat/__init__.rs`) — `fs` alone is split
into 9 crates for what the new tree treats as one subtree,
`pymergetic.metal.fs.*`.

- A crate boundary exists only where a `build` key demands a genuinely
  separate compiled artifact — not one per subsystem.
- Internal module-to-module edges within one artifact are plain
  `use crate::...` paths (Rust's own module system), never a Cargo
  `[dependencies]` entry.
- Target for `metal`: roughly one crate for the whole native tree, plus one
  per Rust-containing wasm/ELF pack — not 28.

---

## Open

- How does a card's exports actually reach `__pm_modules`? Does the module
  author hand-write `pm_mod_export_set`/`pm_mod_publish` calls against the
  emitted `.export.h`/`.export.rs`, or does the generator also emit a
  `*.reg.c`/`*.reg.rs` sibling so registration is never hand-written? This is
  the load-bearing question for converting the 38 C + 43 Rust `RegMod` sites
  — needs an answer before that migration starts.
- ELF `sig` tagging (today: hand-typed `sig = "i32_i32"` per export) could
  instead be derived by parsing the export face's real C/Rust signature.
  More tooling than the rest of this doc; not required to unblock anything
  yet.
- Does `version` (deps resolution) live only on `build`-marked roots, or can
  a non-root module be independently depended-on/versioned too?

## Decision log

| Date | Decision |
|------|----------|
| 2026-08-10 | `src/` only; path == module; one `impl` lang |
| 2026-08-10 | Faces = types / export / import; guard role, split edges |
| 2026-08-10 | Card = `*.pmm.toml` with `impl` only; name from path |
| 2026-08-10 | Retire `pack.toml`; one manifest system (`*.pmm.toml` everywhere) |
| 2026-08-10 | `on_load`/`on_unload`/`deps` move onto the card; dead fields (`type`/`comment`/`description`/`license`) dropped |
| 2026-08-10 | Deliverable root = card with `build` key (array — one tree, N container twins); build-only knobs nest under `[build]` |
| 2026-08-10 | No `[[exports]]`/`[[imports]]` lists (either container) — packer unions module faces under the root's subtree instead |
| 2026-08-10 | Same-artifact cross-module calls: real impl private (`static`/non-`pub`), export face is a slot-backed inline wrapper, slot filled eagerly at connect time, registration = one generator-placed macro beside the impl (no `*.reg.c`) |
| 2026-08-10 | No flat-dump exception for test fixtures — `bridge` gets decomposed into real per-concern child nodes under one `build` root, once tooling exists |
| 2026-08-10 | Crate boundaries follow `build` (genuine separate artifacts) only; kill the 28-crate/one-per-subsystem pattern in `extmod/metal` |
| 2026-08-10 | Py gets a real generated export face too, not a second-class `impl`; only the import face is C/RS-only (Py's `import` is already `sys.modules`) |
| 2026-08-10 | Py signature comes from parsing the function's own type hints via `ast` — never a manifest/card field; same source-is-truth rule as C/RS faces |
