# Module tree (Py / C / RS)

**Status:** redesign in progress — decisions below are locked as we go.

Purpose: one import tree that behaves like normal Python packages, whether the
muscle is Python, C, or Rust.

---

## Locked

### Tree

- One tree under `src/`. **No parallel `include/`.**
- **Path == module:** `src/pymergetic/metal/net/ssh.*` → `pymergetic.metal.net.ssh`.

### Sample (`src/`) — current shape, see "Faces live in the module's own
folder" and "Impl body also moves in" below for the reasoning

```text
src/pymergetic/metal/
├── util/
│   └── string_utils.py              # trivial leaf, no folder needed yet:
│   └── string_utils.pmm.toml        # no children, no cross-language faces
│                                     # → stays a bare sibling pair. impl = "py"
│
├── microdot/                        # →  pymergetic.metal.microdot
│   ├── __pmm__.toml                 # impl = "py"
│   ├── __init__.py                  # package body (Py's own convention)
│   └── request/                     # only if request is its own module
│       ├── __pmm__.toml
│       └── __init__.py
│
└── net/
    ├── ip.h                         # umbrella — canonical include, path == module
    ├── ip/                          # →  pymergetic.metal.net.ip
    │   ├── __pmm__.toml             # impl = "rs"
    │   ├── __impl__.rs              # types SoT lives here too (human)
    │   ├── __types__.h              # emitted (cbindgen)
    │   ├── __exports__.h            # emitted
    │   └── __imports__.h            # emitted or hand
    │
    ├── ssh.h                        # umbrella — canonical include, path == module
    ├── ssh.rs                       # barrel — not optional, makes the Rust path resolve
    └── ssh/                         # →  pymergetic.metal.net.ssh
        ├── __pmm__.toml             # impl = "c"
        ├── __impl__.c
        ├── __types__.h              # types SoT (human)
        ├── __exports__.h            # human provide face (role-guarded)
        ├── __imports__.h            # human / connect needs
        ├── __exports__.rs           # emitted (bindgen)
        ├── __imports__.rs           # emitted or hand
        │
        └── key/                     # real child of ssh (only when needed) —
            ├── __pmm__.toml         # coexists with the dunder files above
            ├── __impl__.c           # it, no collision: dunder names are
            ├── __types__.h          # reserved, same reasoning as __init__.py
            ├── __exports__.h
            └── __imports__.h
```

Legend: module node = folder (named after the module) holding `__pmm__.toml`
+ muscle + faces, all dunder-named so real children can live in the same
folder without collision. No `include/` twin tree. A module with no
children and no cross-language face needs at all can skip the folder
entirely and stay a bare sibling pair (`x.py` + `x.pmm.toml`) — don't create
an empty ceremony folder for something that has nothing to put in it yet.

### Shape for `A.B.C`

| Lang | Module body | Children |
|------|-------------|----------|
| C | `A/B/C/__impl__.c` (+ faces) | `A/B/C/<child>…` |
| RS | `A/B/C/__impl__.rs` (+ faces) | `A/B/C/<child>…` |
| Py | `A/B/C/__init__.py` (+ faces if any) | `A/B/C/<child>…` |

Py keeps `__init__.py` as its impl filename (matches Python's own
convention, and nothing about `path == module` needs it renamed) rather
than `__impl__.py`.

### One defining lang

- Exactly one of C / RS / Py **defines** the module.
- Other langs only **consume** via faces.

### Module card (once, forever)

**`__pmm__.toml`** inside the module's own folder (`pmm` = pymergetic
module) whenever that folder exists; a bare sibling `A/B/C.pmm.toml` only
for a folder-less trivial leaf (see "Sample" above).

```toml
fqn = "pymergetic.util.mem"
impl = "c"   # c | rs | py
```

- **Required keys:** `fqn` (always first) and `impl` (or `pep420 = true`
  for a namespace node).
- **`fqn`** = fully-qualified dotted module name. **Path stays the actual
  source of truth** — `fqn` doesn't change that, it's a **checked
  assertion**, not an independent declaration. Tooling always derives the
  real name from the path and hard-errors if the card's `fqn` disagrees,
  rather than trusting whichever is more convenient. Two things that buys,
  cheaply: a fast, loud catch for path/rename mistakes (move or rename the
  folder, forget the card, tooling fails immediately instead of silently
  importing under the wrong name), and a card that's self-describing on
  its own — useful now that a card can sit several dunder-folders deep
  (`pymergetic/util/mem/__pmm__.toml`) with nothing in its own filename
  hinting at what it's for. Named `fqn` rather than `name` so it's
  unambiguous that it's the full dotted path, not a leaf/short name.
- `fqn` is written first in every card, by convention, so opening any
  `__pmm__.toml` cold immediately answers "what module is this".
- More keys may appear later in the **same** file; no `*.pmm.impl` sidecars.

### Namespace nodes (PEP 420)

Some path segments aren't owned by any single distribution — `pymergetic`
itself, and `pymergetic.util`, are meant to be contributed to independently
by whichever repo needs them (`metal`, `wasmmod`, `metalpython`, ...), same
as Python's own [PEP 420](https://peps.python.org/pep-0420/) implicit
namespace packages: no single `__init__.py`, no single `impl`, just a
directory that different installs each add children under.

Card flag, **no `impl` key** — there's no muscle file to pick a language for:

```toml
fqn = "pymergetic"
pep420 = true
```

- `src/pymergetic/__pmm__.toml` → `pep420 = true`
- `src/pymergetic/util/__pmm__.toml` → `pep420 = true`
- Every other package-shaped node (e.g. `microdot/` in the sample above,
  and `wasmmod/` itself — see below) defaults to a **regular** package
  instead: one `impl`, one `__init__.*` body, single-sourced from one
  distribution.

**The test is "can another distribution ever add a child here", not
"does it happen to have children".** `pymergetic` and `pymergetic.util`
genuinely can gain a sibling from some other package someday — that's
the entire reason PEP 420 exists. `wasmmod` can't: it's one closed
project, everything under `pymergetic.wasmmod.*` is defined right here,
nothing external ever contributes a child. So `wasmmod/__pmm__.toml` is
`impl = "py"` with a real (if trivial) `__init__.py`, **not**
`pep420 = true`, even though it has a folder full of children exactly
like the namespace nodes do. Having children doesn't make something a
namespace; being *open to contribution from elsewhere* does.

### Rust and PEP 420 — it doesn't have one, and that has a real consequence

PEP 420 works because Python import resolution is a **runtime** step: the
interpreter scans `sys.path` and merges any directories sharing a dotted
prefix, from however many independently-installed distributions happen to
be present, with zero declared coordination between them. Rust has no
equivalent step, period — everything is resolved and linked at **compile
time** from one fixed, known dependency graph. A crate is a single-owner
compilation unit; two separate crates can never both *be*
`pymergetic::util` the way two separate pip packages can both be
`pymergetic.util`. The only way to make one crate's content appear under
another's path is an explicit, one-directional `pub use` — always a
deliberate re-export, never a dynamic multi-owner merge.

Concrete consequence: **every level of the tree still needs a real Rust
declaration file, even the `pep420 = true` ones.** Python needs zero bytes
for `pymergetic/__pmm__.toml`'s namespace; Rust needs at least one `mod`
declaration there regardless, because there's no filesystem-scan step to
fake it with. That declaration file carries zero logic and isn't an
"impl" in the card's sense — it's pure mechanical plumbing Rust's
compiler demands, same status as any other barrel/umbrella file (see
`pymergetic/util.rs`, which does nothing but declare `mem`/`zlib`/`mtar`/
`lz4`/`lock`/`pysample` as children).

### Crate placement

One crate today (`wasmmod` is the only `build`-marked deliverable that
exists) — a Cargo **workspace** only earns its keep once a second,
genuinely separate deliverable crate shows up (metal's own crate,
depending on this one, later). `Cargo.toml` is build config, so it lives
at the wasmmod repo root, a sibling of `src/`/`docs/`/`dev/`/`tools/`,
never inside `src/` (same reasoning as "`src/` holds module content only,
scaffolding stays out" already established above).

`src/lib.rs` is Cargo's own hardcoded entry-point filename — a mechanical
requirement, not a "module" in the `pymergetic` dotted-tree sense, same
non-status as `Cargo.toml` itself. It doubles as the one declaration file
`pymergetic`'s own crate-root level needs (see previous section) — no
separate `pymergetic.rs` sits next to it, since crate root and "the
`pymergetic` namespace's Rust half" are the same thing here. Consumers
depend on this crate under the **local name** `pymergetic`
(`pymergetic = { package = "pymergetic-wasmmod", path = "..." }` in their
own `Cargo.toml` — an ordinary, well-supported Cargo pattern), so
`use pymergetic::util::mem;` reads exactly like the dotted path it
mirrors, with no `pymergetic_wasmmod::pymergetic::` stutter.

(Card placement — beside vs. inside — is the general rule right below;
namespace nodes always have a folder, so they always use `__pmm__.toml`.)

### No `pack.toml` — one manifest system

`pack.toml` is retired. Everything it did either dies or moves onto the
card of whichever node is a deliverable root:

| `pack.toml` had | Now |
|---|---|
| `type`, `comment`, `description`, `license` | **gone** — grepped every tool, nothing ever read these; pure decoration |
| `name` | gone as a free-form label — reborn as `fqn` on the card (see "Module card" above), a checked assertion against the path, not an independent name |
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
  else already built — `pm_wasmmod_registry_connect_import`/`pm_wasmmod_registry_connect_guest`), not
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
  `pm_wasmmod_registry_export_set` calls, no sibling reg file — one generator-placed macro
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
- Umbrella `C.h` recombines the faces at the module's real path — this is
  the canonical include (**compliance**, not convenience). Including a
  face directly (`mem/__exports__.h`, say) still compiles, but that
  couples the caller to internal face layout instead of the module's
  actual `path == module` path — exactly the kind of coupling that broke
  when faces got restructured into their own folder (see decision log).
  Same for the `.rs` barrel: for Rust it's not even "prefer this", it's
  the only thing that makes `use pymergetic::util::mem` resolve at all.
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

### Picking `impl` for `c`/`rs` when it's actually a free choice

Direct C↔Rust FFI is trampoline-free either direction — `extern "C" fn`
(Rust) and a plain C symbol link to the exact same ABI, no shim needed
either way. The wasm-guest thunk/trampoline machinery (`forward_raw`,
thunk pools, `pm_wasmmod_thunk_export`) is a separate concern — crossing the
VM boundary costs the same regardless of which language wrote the guest
module. So direction never turns on trampolines. It turns on tooling
risk and how much of the implementation stays safe:

- **Default to `impl = "rs"` (cbindgen → `.h`), not `impl = "c"` (bindgen
  → `.rs`).** `cbindgen` is a pure `syn` AST walk over your own
  `#[repr(C)]` source — no compiler invocation, no target triple, no
  `-I` flags, no sysroot. `bindgen` going the other way has to run a real
  clang parse with the real build's flags, for targets that in this repo
  are sometimes freestanding (`wasm32-unknown-unknown`, bare-metal
  ELF/EFI) — exactly the kind of cross-target header-parsing fragility
  the `-I` root-anchoring rule above already exists to fight. `cbindgen`
  structurally can't drift that way; there's no compiler invocation to
  desync from the real build.
- Writing the **implementation** in Rust also means only the thin
  `extern "C" fn` boundary is stuck in C's type vocabulary (primitives,
  pointers, `repr(C)` structs); everything behind it keeps real memory
  safety, enums-with-data, iterators. A C implementation has no
  equivalent richer inner dialect to retreat into — it's stuck at ABI
  level top to bottom.
- **`impl = "c"` is still the right, non-optional call when wrapping an
  existing C library** (`mem` → TLSF, `zlib` → `uzlib`, this same
  `util/` tree) — there's no new logic to author there, just an existing
  C API to face; `mtar`/`lz4` (own logic, free choice) went `rs` on
  purpose, same tree, for exactly the reasons above.

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

Hint → `pm_wasmmod_pyexport_export_py*` shape mapping (reusing the exact taxonomy that
family already has):

| Hint | Shape |
|---|---|
| `int -> int` | `i32` (default) |
| `bytes -> int` | `mem` — matches `pm_wasmmod_pyexport_export_py_mem` exactly: native side gets an `int32` cookie, marshals to `bytes` *before* calling Python; the `bytes` annotation on the Python side already **is** that marshaling declaration |
| unannotated / arbitrary object param | `obj` (handle-resolved) |
| `pymergetic.wasmmod.types.i64` / `f32` / `f64` | disambiguates where Python's own `int`/`float` are too coarse — project-provided aliases, still 100% derived from source via `ast`, never a manifest |
| `pymergetic.wasmmod.types.RawBuf` | the ELF-only `bufptr` shape — explicit opt-in marker, never inferred, since it's a real host pointer and already flagged as unsafe for a wasm guest in `pyexport.h` |

The packer, parsing a `py` module's functions this way, generates **both**:

- `util.export.h` / `util.export.rs` — same slot-backed inline wrapper shape
  as a `c`/`rs` export (see "Same-artifact calls stay private" — nothing
  about being Python exempts a same-artifact call from going through a slot).
- the registration call itself (picks the right `pm_wasmmod_pyexport_export_py*` variant
  off the parsed hint) — replaces every hand-written `_wasm.export_py(...)`
  call that today lives inside `__init__.py` (see `bridge`/`hello.util`'s
  current source — that boilerplate goes away once this generates).

### Python face — the mirror direction (`impl` is `c`/`rs`, not `py`)

The above is Python **exporting**. The other direction — editor/typing
support for a **non**-`py` module, so `import pymergetic.util.zlib` gets
real completions/types even though there's no `.py` source to read hints
from — needs its own face: `__init__.pyi`.

No new mechanism to design here — Python's own leaf-vs-package duality
already **is** the "bare sibling vs. folder" rule this tree uses
everywhere else:

| Shape | Where |
|---|---|
| Trivial leaf, `impl = "py"` | `xyz.py`, direct sibling — same as today's Python |
| Package, `impl = "py"` | `xyz/__init__.py` inside the module's own folder — same folder faces already live in |
| `impl` is `c`/`rs` | `xyz/__init__.pyi` inside the module's folder, alongside `__types__.h`/`__exports__.*` — **generated**, same status as any other mirror face, never hand-maintained |

Two things keep this honest instead of a blind ABI-to-`.pyi` transcription:

- **Not every export is Python-visible.** Only the ones actually routed
  through the `pm_wasmmod_pyexport_export_py*` family (see "Py export face" above, same
  taxonomy, just read backwards) show up in the stub. `mem`'s exports are
  raw arena/pointer internals with no Python marshaling at all — its
  generated `__init__.pyi` is legitimately near-empty, and that's correct,
  not a missing face.
- **`.pyi` is pure type-checker sugar.** Nothing at runtime reads it — the
  real object Python sees at `import` time still comes from
  `__pm_modules`/`sys.modules` registration, exactly like every other
  non-`py` module. Deleting the `.pyi` changes zero runtime behavior, only
  editor completions.

### Naming

- No `.gen.` infix — `types` / `export` / `import` already mark face files.
- Banner / tooling owns “do not edit” on emitted faces.
- C-defined: human muscle = `C.c`. Optional human `C.rs` = **barrel reexport** of faces only (not a second impl).

### Exported symbol names: filepath == modulepath == the name, `pm_` is the only elision

Every symbol that crosses the flat C ABI namespace — every `#[unsafe(no_mangle)]`
Rust fn, every plain C fn declared in a face, every `#[repr(C)]` type/enum
those faces expose, every `PM_*` constant — is named after its **full**
module path, dots turned into underscores. `pm_` is the *only* thing
allowed to stand in for a path segment, and it only ever stands in for
`pymergetic` (the one segment every single module shares, so spelling it
out every time buys nothing). Nothing else is elidable, "obvious", or
"implied by the file it's in" — the reason to encode the path into the
name at all is that C's linker namespace is flat with no real module
system backing it up, so the encoded name **is** the only namespacing
mechanism C symbols get. Dropping a segment "since it's obvious from
context" defeats the entire point — there is no context once the symbol
is loose in one flat table.

| Module (fqn) | Symbol prefix |
|---|---|
| `pymergetic.util.mem` | `pm_util_mem_` |
| `pymergetic.util.lock` | `pm_util_lock_` |
| `pymergetic.wasmmod.registry` | `pm_wasmmod_registry_` |

This does **not** apply to a Rust-only identifier that never crosses FFI
(a plain `pub fn`/`fn` with no `#[unsafe(no_mangle)] extern "C"`, e.g.
`lz4_compress_block` inside `pymergetic.util.lz4`) — Rust already has a
real hierarchical module system, `pymergetic::util::lz4::lz4_compress_block`
is already fully namespaced by the path itself, encoding it into the
identifier a second time would be pure noise. The rule is specifically
for symbols that leave that namespacing behind by becoming a raw,
flat-namespace C ABI export.

### Faces live in the module's own folder, dunder-named

A leaf module with faces in every language accumulates 5-9 sibling files
fast (`x.pmm.toml`, `x.c`/`x.rs`, `x.h`, the human-authored primary faces,
*and* their foreign-language mirrors) — the containing directory stops
being scannable well before the module tree itself gets big. Faces move
into the module's own child-folder instead:

```text
util/
├── mem.c            # impl
├── mem.h            # umbrella — canonical include, path == module
├── mem.rs           # barrel — not optional, makes the Rust path resolve
├── mem.pmm.toml      # card
└── mem/
    ├── __types__.h
    ├── __exports__.h
    └── __exports__.rs
```

That folder is the exact same folder "path == module" already reserves for
**children** of `mem` — reusing it for faces would collide with a real
future child unless the face filenames are unambiguously reserved. Dunder
names (`__types__.*`, `__exports__.*`, `__imports__.*`) solve this the same
way Python already solved it for `__init__.py`: no real submodule is ever
named that, so a real child (`mem/pool.rs`, say) and the generated faces
coexist in the same folder without any naming conflict, in any language,
including a `py`-impl child using its own `__init__.py` there.

Originally the card (`.pmm.toml`) stayed a direct sibling and only the
faces moved in — see "Module card" above for why it moved in too
(`__pmm__.toml`), leaving only the umbrella/barrel routing files as
siblings (still the canonical, compliant include point — see "Faces
(types / export / import)" above; not present at all only for a module
no other language ever needs to reach).
Everything under the dunder names is either emitted or, until the codegen
exists, a hand-written stand-in for what will be emitted.

### Impl body moves in too — `__impl__.*`

Same folder, one more dunder name, asymmetric by language for a real
reason:

- **C:** `mem.c` → `mem/__impl__.c`. Nothing in the upper dir needs to
  change or exist to compensate — C has no source-level import-path
  concept, a consumer only ever goes through the umbrella header
  (`mem.h`), and that header already points into the folder. Physically
  moving the `.c` file changes nothing a C consumer can observe.
- **RS:** `lz4.rs` → `lz4/__impl__.rs`, but then the upper dir needs a
  **tiny reexport shim** at `lz4.rs`:

  ```rust
  #[path = "lz4/__impl__.rs"]
  mod r#impl;
  pub use r#impl::*;
  ```

  Rust, unlike C, has a real source-level module-path system — some file
  has to exist at the path `path == module` promises (`util/lz4.rs` →
  `pymergetic.util.lz4`) for `use`/`mod` resolution to work at all. The
  shim is that file; the actual few-hundred-line implementation is tucked
  away in the folder with everything else non-trivial. `#[unsafe(no_mangle)]`
  exports inside `__impl__.rs` are unaffected either way — those are
  resolved by raw symbol name after linking, not by module path.

Net effect at the time this was written: every module's upper-dir
footprint was `*.pmm.toml` + thin routing files. Superseded a section down
("Module card") — the card moved in too, so the upper dir is now *only*
the umbrella/barrel routing files, nothing else.

### C/C++ includes: full path from a fixed root, never `../../..`

A small, fixed set of `-I` roots, each `#include` always spelled out in
full from one of them — never a same-directory bare include, never a
chain of `../`:

| Root | `-I` (already in `.clangd`, nothing new needed) | Example include |
|---|---|---|
| wasmmod repo root | `-I.` | `#include "src/pymergetic/util/mem/__exports__.h"`, `#include "third_party/tlsf/tlsf.h"` |
| MicroPython TOP | `-I../..` | `#include "lib/uzlib/uzlib.h"` (same convention upstream's own `extmod/moddeflate.c` already uses) |

Two concrete reasons, not just taste:

- **Stays correct across file moves.** A `../../../../third_party/tlsf`
  relative include had its dot-count bumped from 3 to 4 the moment
  `mem.c` became `mem/__impl__.c` — the include text depended on the
  including file's own depth. `#include "third_party/tlsf/tlsf.h"`
  wouldn't have needed to change at all.
- **`-I` list stays small and fixed.** Adding a directory-specific `-I`
  for every subtree that wants short includes is exactly the kind of
  clutter that compounds — two roots cover the whole tree instead.

Doesn't apply to Rust's `#[path]` (e.g. `mem.rs`'s
`#[path = "mem/__exports__.rs"]`) — that's a per-declaration relative
directive intrinsic to the language, not a search-path list, so there's
no equivalent clutter problem for it to solve.

### Tooling (intent)

- **bindgen:** C → RS consume faces (`types` / `export`; `import` only if mechanical).
- **cbindgen:** RS → C consume faces.
- Soft-connect `.import` may use a small custom emitter or stay hand-written.
- Neither tool covers: cross-module `.import.*` wiring (each only mirrors
  *your own* API, not someone else's), registration into `__pm_modules`,
  WASM-vs-ELF target branching for a `build` array, or anything touching
  Python faces (`wasm.export_py*`/`pm_wasmmod_pyexport_export_py`) — no generic tool
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
  author hand-write `pm_wasmmod_registry_export_set`/`pm_wasmmod_registry_publish` calls against the
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
| 2026-08-11 | Namespace nodes get a `pep420 = true` card flag (no `impl`) instead of a muscle file; `pymergetic` and `pymergetic.util` are the first two |
| 2026-08-11 | Faces move into the module's own child-folder, dunder-named (`__types__.*`/`__exports__.*`/`__imports__.*`) — reuses the same folder "path == module" already reserves for children, collision-free because dunder names are reserved the same way `__init__.py` already is; only card/umbrella/barrel stay as direct siblings |
| 2026-08-11 | Impl body also moves into the folder as `__impl__.c`/`__impl__.rs`. C needs nothing left behind (header is already the single entry point); RS needs a tiny reexport shim at the old path so `path == module` still resolves for real `use`/`mod` — `#[unsafe(no_mangle)]` exports are unaffected regardless |
| 2026-08-11 | C/C++ includes always full-path from a fixed root (wasmmod repo root via `-I.`, or MicroPython TOP via `-I../..`) — never bare same-dir, never `../../..` chains; keeps includes stable across file moves and keeps the `-I` list from growing per-subtree |
| 2026-08-11 | Card moves in too: `__pmm__.toml` inside the module's folder whenever one exists (children, faces, or hidden impl), same reasoning as `__impl__.*`/faces above; a folder-less trivial leaf keeps a bare sibling `x.pmm.toml` instead of an empty ceremony folder |
| 2026-08-11 | Card gains a required `fqn` field (dotted path, written first) — path stays the real source of truth, `fqn` is a checked assertion tooling hard-errors on if it disagrees; catches path/rename mistakes and makes a buried `__pmm__.toml` self-describing on its own |
| 2026-08-11 | Non-`py` modules get a generated `__init__.pyi` face (editor/typing only, nothing at runtime reads it) sourced from whichever exports are actually routed through `pm_wasmmod_pyexport_export_py*` — not a blind ABI mirror, and legitimately near-empty for a module with no Python-visible surface (e.g. `mem`) |
| 2026-08-11 | `pymergetic.util.pysample` added: the training set's one real `impl = "py"` leaf (`hello`/`echo_len`), so "Py export face" has a live worked example, not just C/RS ones — hand-written `__exports__.h`/`__exports__.rs` stand in for the not-yet-built py-facegen, same posture as the mtar/lz4 bindgen/cbindgen stand-ins |
| 2026-08-11 | When `impl` is a free choice between `c`/`rs` (i.e. not forced by wrapping an existing C lib), default to `rs` (cbindgen → `.h`) over `c` (bindgen → `.rs`) — `cbindgen` is compiler-invocation-free (no target/`-I` drift risk across unix/wasm32/ELF-metal/EFI targets), and only the thin `extern "C"` boundary loses Rust's safety, not the whole impl |
| 2026-08-11 | `pep420` test is "can another distribution add a child here", not "does it have children" — `wasmmod/` has a folder full of children but is one closed project, so it's `impl = "py"` with a real `__init__.py`, not `pep420 = true`; only `pymergetic`/`pymergetic.util` (genuinely open to outside contributions) get the namespace flag |
| 2026-08-11 | Umbrella `.h` / barrel `.rs` are the **compliant** path-`==`-module include, not a "convenience, take it or leave it" alternative to including a face directly — direct-face-include compiles but couples callers to internal layout that's explicitly allowed to move |
| 2026-08-11 | Rust has no PEP-420 equivalent (compile-time single-owner linking, no runtime namespace merge) — every tree level still needs a real `mod` declaration file even under a `pep420 = true` card; that file is pure mechanical plumbing, not an "impl" |
| 2026-08-11 | One crate (`pymergetic-wasmmod`), `Cargo.toml` at the wasmmod repo root (sibling of `src/`, never inside it); `src/lib.rs` doubles as the `pymergetic` crate-root declaration (no separate `pymergetic.rs`); consumers alias the dependency to the local name `pymergetic` so `use pymergetic::util::mem` has no stutter |
| 2026-08-11 | Every C-ABI-crossing symbol (fn, `#[repr(C)]` type, `PM_*` const) is named after its **full** module path, dots to underscores, `pm_` the only elidable segment (stands only for `pymergetic`) — fixed a real bug where `registry` used a bare invented `pm_mod_` prefix (neither `wasmmod` nor `registry`) and `mem`/`zlib`/`lock`/`pysample` dropped `util`; renamed all of them (`pm_wasmmod_registry_*`, `pm_util_mem_*`, `pm_util_zlib_*`, `pm_util_mtar_*`, `pm_util_lz4_*`, `pm_util_lock_*`, `pm_util_pysample_*`) |
| 2026-08-11 | Corrected a wrong call on `lock`'s `__init__.pyi`: it had claimed Python "never" takes this lock, which is false — upy already runs a real thread mutex on ports with `MICROPY_PY_THREAD` on (unix, on by default) — so `lock` is Python-reachable (`Lock` class) like any other module, not fenced off; it stays a genuinely *separate* primitive from upy's GIL/`mp_thread_mutex_t` though, not merged into it — `pm_util_lock_t` is pure-spin/no-OS (must work pre-VM, bare metal, wasm32), the GIL mutex is deliberately OS-blocking once real threads exist, and neither can be the other without a regression; `mem` stays the one legitimately near-empty `.pyi` (no Python-visible surface at all, not "Python doesn't need it") |
| 2026-08-11 | `pymergetic.util.mem` gets the real dual-span arena (map bump-up / TLSF heap bump-down with lazy `tlsf_add_pool` growth from the shrinking hole, same shape as metal's proven `pymergetic.metal.mem.{arena,tlsf}` + `mem/port/mem.c`) instead of the flat single-pool TLSF wrapper it had — metal is meant to consume this, not carry its own second copy; uses `pymergetic.util.lock` for the arena's mutex (one mechanism, not two); "dual arena" as caller usage pattern (independent `pm_util_mem_arena_create` calls) still composes on top, unaffected — the two "dual"s are orthogonal (per-arena map/heap split vs. N-arena instantiation) |
| 2026-08-11 | `heap_grow_pool`'s new-pool sizing needs `need + need/16` slack, not just fixed pool/alloc overhead (metal's original formula) — TLSF is segmented-fit and rounds a request up to a size class before searching, so a pool sized at exactly request-plus-tiny-overhead can still miss on large near-exact-fit requests; caught by a deliberately adversarial test (`request == 2x the fresh pool's own size`), not by normal-sized real usage — worth backporting to metal's copy too since the mechanism is identical there |
| 2026-08-11 | Rust crate can't get `crate-type = ["lib", "staticlib"]` yet: this `#![no_std]` crate has no `#[global_allocator]`/`#[panic_handler]`, and rustc requires both the moment `staticlib` is one of the requested outputs, for the *whole* crate — breaks `cargo check`/`cargo test` too, not just the new target; reverted. Verified `mem`'s new C code against the real (not stubbed) `pm_util_lock_*` anyway by compiling `lock/__impl__.rs` alone as an ad hoc `std`-mode staticlib (no `#![no_std]` on that file itself) — good enough for a host-only correctness check, not a substitute for solving the real allocator/panic-handler question before any Rust module can actually link into a C/metal binary |
