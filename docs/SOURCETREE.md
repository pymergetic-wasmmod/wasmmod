# Module tree (Py / C / RS)

**Status:** redesign in progress — decisions below are locked as we go.

Purpose: one import tree that behaves like normal Python packages, whether the
muscle is Python, C, or Rust.

### Two `wasmmod` trees — don't confuse them

- `packages/metalpython/extmod/wasmmod/` — **old, read-only reference.**
  Pinned to `metalpython`'s `master` branch's own submodule commit. Still has
  the real `pack.toml`-based packer (`dev/tools/.../tools/pack.py`,
  `pack_elf.py`, `pack_tree.py`), every example package (`hello`, `mixed`,
  `client`, `ticks`, `ticks_elf`, `host_elf`, `bridge`, `tree/*`, ...), and the
  `run_matrix.py`/`run_elf.py`/`Makefile` test runners. Port **from** here;
  never edit it as part of this redesign.
- `packages/metalpython-wasmmod/extmod/wasmmod/` (this tree) — **the
  destination.** Blank-main rewrite this doc's own decisions apply to. Port
  **to** here.

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

### Wait class (on wait-y export faces)

Not a card key. Not a second module tree. Not an async engine in wasmmod.

Wait-y exports are tagged `sync` | `facade` | `async` on the **face**
(comment/convention this slice; not a `pmm-parser` TOML key yet):

| Tag | Meaning |
|---|---|
| `sync` | bounded CPU; never waits (`uri_is_http`, `join_uri`, `set`/`get`) |
| `facade` | enqueue / checkpoint; never parks (`yield`) |
| `async` | may wait; Metal parks **inside the same symbol**; mpwm/unix may **block** in the same fill (`fetch`, `probe`, `request`) |

Engine stays in Metal ("Py async = Metal async"). Wasmmod owns the async
**border**: `io_ops` is sync-looking C; the fill is where wait happens.
`build = ["wasm", "elf"]` remains the only artifact twin. Do not invent
`foo_async/` trees or a second asyncio.

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
| `python.mount` / `.freeze` / `.targets` / `.keep_source`, `[source]`, `[pack]` | → nest under `[build_cfg]` on the root card only (see below) |

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
version = "0.1.0"         # publish-unit version (roots + kernel modules)

[build_cfg]
mount = "mount"           # dir of raw files (incl. .py) embedded as pack payload —
                           # data-plane only, not a node in the fqn/impl tree
freeze = true
targets = ["upy:mpy6:sib31", "cpy:cp312"]
keep_source = true
embed_source = true
compress_source = true
compress_pack = true
```

(Named `[build_cfg]`, not `[build]` — `build = [...]` above is already a
top-level array assignment to the key `build`; a `[build]` *table* header
would try to redeclare that same key, which is a real TOML parse error
(`Cannot declare ('build',) twice`), found while implementing `pmm-parser`.)

`mount` is a `[build_cfg]`-only knob, not a module-tree concept: the files
under it (typically the pack's own `__init__.py` + whatever it imports) are
embedded verbatim into the pack's payload section for the Python VM to
mount at runtime, same as `pack.toml`'s old `python.mount` — this is a
data-plane relationship, not a node in the fqn/`impl` tree, so it doesn't
have to (and shouldn't) satisfy "one defining lang" — the mount tree can
happily contain the pack's Python-visible surface even though the same
root's own card is `impl = "c"`.

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

### Recursive build boundary

A node inside a `build`-marked root's own subtree can itself carry a
`build` key — this isn't an edge case to design around, it's a real,
already-existing shape: once `tree/test_a_test_d/` becomes a real nested
directory (`test_a/test_d/`, per `fqn`-must-match-path), it sits *inside*
`test_a`'s own subtree while remaining its own separate `.wasm` artifact
(own exports `d_value`/`d_rs_value`, nothing to do with `test_a`'s own
`a_ping`/`a_rs_ping`) — exactly as today's flat `tree/test_a/` and
`tree/test_a_test_d/` are already two independent pack builds, just about
to become path-nested instead of name-dashed.

Rule: a nested `build` key is a **hard boundary**, not an invitation to
bundle. When a packer run is producing the artifact for root *R*, it walks
*R*'s subtree unioning faces/sources/deps as normal, but **stops descending
the instant it reaches a child card that itself has a `build` key** — that
child's whole subtree belongs to its *own* build, not *R*'s. `R`'s own
compiled output never contains a byte of it. The nested root is built
entirely separately (its own invocation, own artifact, own `deps`/`version`)
and merely happens to live at that path because `fqn`/path-nesting says so,
not because of any compiled relationship between the two. If the outer
root's own faces need something from the nested one at runtime, that's an
ordinary cross-artifact `deps` + import-face relationship (guest-guest),
identical in shape to depending on a root that isn't nested under you at
all.

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
| `py` | **the function's own type hints** — scanned in-crate from `__init__.py` (same posture as bindgen/cbindgen reading real source, not a manifest) | `*.export.h` + `*.export.rs`, generated from the parsed hints, same as any other module |

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
scanned in-crate from `__init__.py` (never executed — same posture as
bindgen/cbindgen reading real source):

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
| `() -> int` / `int -> int` (0..3×`int`) | `i32` — `int32_t(void)` / `int32_t(int32_t, …)` |
| `bytes -> int` | `bufptr` — `(const uint8_t *, uint32_t) → i32`; wraps `[ptr,len)` as `bytes` before calling Python |
| `"mem" -> int` | cookie **mem** — `int32_t(pm_wasmmod_mem_cookie_t)`; host table borrows `[ptr,len)` for the call |
| `"obj" -> int` | handle **obj** — `int32_t(pm_wasmmod_obj_handle_t)`; GC-rooted `mp_obj_t` slot |

Live today: the rows above (`util.gen` discover + `pm_wasmmod_pyexport_*` + host memcookie/objhandle). Discover emits 0..3×`int`, `bytes`/`mem`/`obj`, and scalar aliases `i64`/`int64`, `f32`/`float`, `f64`/`float64`. Do not invent a `pymergetic.wasmmod.types` module in docs until it exists as a real product face.

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
| `pymergetic.wasmmod.loader` | `pm_wasmmod_loader_` |

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
| wasmmod repo root | `-I.` | `#include "third_party/tlsf/tlsf.h"` |
| wasmmod `src/` | `-Isrc` | `#include "pymergetic/util/mem/__exports__.h"` (**never** spell `src/` inside the include) |
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

### Schema reference (consolidated)

Everything below is already decided above; this is the flat lookup table
`pmm-parser` gets implemented against, so it doesn't have to be re-derived
from the rationale prose each time.

**Card keys** (`*.pmm.toml` / `__pmm__.toml`):

| Key | Required? | Type | Notes |
|---|---|---|---|
| `fqn` | required, always first | string (dotted) | checked assertion against the real path, not an independent declaration |
| `pep420` | required if this is a namespace node | bool | mutually exclusive with `impl`; namespace nodes have no muscle file |
| `impl` | required unless `pep420 = true` | `"c"` \| `"rs"` \| `"py"` | exactly one defining language per module |
| `on_load` / `on_unload` | optional | string | lifecycle hook function name; any module, not just roots |
| `deps` | optional | table (`{ "dotted.fqn" = "version" }`) | **`build`-marked roots only** — pins checked at load via `pymergetic.util.version` against the dependency's **registry** version |
| `version` | optional | string | **Publish units:** `build`-marked deliverable roots **and** host/kernel modules that are independently dependable (e.g. `pymergetic.wasmmod`). Leaf modules inside a pack stay unversioned (baked into the root). Baked into `wasmmod.pkg` (always) and registry on load/init. |
| `build` | optional, presence marks a deliverable root | array of `"wasm"` \| `"elf"` | one source subtree, N compiled container twins; **cannot nest** — see "Recursive build boundary" below |
| `[build_cfg]` sub-table | only meaningful with `build` set | — | `mount` (path, data-plane only — see "Deliverable root"), `freeze` (bool), `targets` (list of `upy:mpyN:sibM` / `cpy:cpNNN` strings), `keep_source` (bool), `embed_source` (bool), `compress_source` (bool), `compress_pack` (bool) |

No `[[exports]]`, `[[imports]]`, `sig`, `name`, `type`, `comment`,
`description`, `license`, `native.dir`/`native.sources` — all retired (see
table earlier in this doc for where each one's job went). Wait class
(`sync` / `facade` / `async`) is **face documentation**, not a card key.

**Face files**, by `impl`:

| `impl` | Muscle (SoT) | Types | Export | Import | Py stub |
|---|---|---|---|---|---|
| `c` | `__impl__.c` | `__types__.h` (human) | `__exports__.h` (human) + `__exports__.rs` (bindgen) | `__imports__.h` (human) + `__imports__.rs` (bindgen or hand) | `__init__.pyi` (generated) |
| `rs` | `__impl__.rs` (types SoT too) | `__types__.h` (cbindgen) | `__exports__.h` (cbindgen) | `__imports__.h` (emitted or hand) | `__init__.pyi` (generated) |
| `py` | `__init__.py` (types = the function's own hints, in-crate scan) | — (hints are the SoT) | `__exports__.h` + `__exports__.rs` (generated from hints) | — (Py has no import face; `sys.modules` already location-transparent) | n/a (it *is* the source) |
| `pep420` | — (no muscle file) | — | — | — | — |

**Module tests** (not faces — never emitted by `util.gen`):

| File | Role |
|---|---|
| `__tests__.rs` / `__tests__.c` / `__tests__.py` | Language entry beside the card; cases register via `PM_MOD_TEST_RS!` / `PM_MOD_TEST_C` into a **parallel test table** on the same `ModEntry` |
| `__tests__/` | Optional split of cases (included by the entry) — **not** a new fqn / no `__pmm__.toml` |
| Case ABI | `extern "C" fn() -> i32` — `0` pass, nonzero fail |
| Host runner | `util::mod_test::registry_mod_tests_all` walks every registered module's tests; filter with `WASMMOD_TEST_FQN` |
| Guest pack | `__tests__.c` linked into the chromosome; packer writes `wasmmod.tests` (MPTE); loader claims test trampolines (never product exports). `__tests__.*` also embed in `wasmmod.source` |
| In-bin UI | `wasmmod.test` / `test_all` / `tests` / `test_count` over the same registry table |

Do not put `#[cfg(test)]` blobs in `__impl__` — keep muscle and tests separated. RS wires `__tests__.rs` as a `#[cfg(test)]` submodule of `__impl__.rs` (private helpers stay reachable).

Every `impl` also needs, as **direct siblings** of the module's folder (or
the module folder itself, for `pep420`): the card (folder case:
`__pmm__.toml` inside; folder-less trivial leaf: bare sibling `x.pmm.toml`),
an umbrella `<name>.h` (C canonical include, skip only if no C/RS consumer
ever needs to reach this module), and a barrel `<name>.rs` (Rust module
resolution — not optional the moment any Rust code, including a `pep420`
node's own children, needs to resolve the path).

**Exports/imports**: never declared in the card. The packer unions whatever
`PM_MOD_EXPORT_C`/`PM_MOD_EXPORT_RS` macro invocations (or, for `py`,
whatever functions get routed through `pm_wasmmod_pyexport_export_py*`) exist
in a deliverable root's subtree. ELF's signature tag (`i32`, `i32_i32`, ...)
is derived the same way — parsed straight from the literal C-type argument
already passed to `PM_MOD_EXPORT_C`/`PM_MOD_EXPORT_RS` at each call site
(e.g. `int(int, const ssh_opts_t *)`), reusing the same tag vocabulary the
old `pack.py`'s `sig_tag()` had, just sourced from real code instead of a
hand-typed string — never a new manifest field.

**Guest→host imports** (module not under `pymergetic.*` — e.g.
`wasmmod.host`, `wasmmod`, `micropython.runtime`) still get a normal import
face (the module genuinely needs that host symbol), but never a `deps`
entry: `deps`/`version` resolution is a pymergetic-pack-tree-only concept;
host-intrinsic modules are resolved directly by the loader/registry, not by
card-to-card dependency.

### Target shapes for pending example conversions

Dry-run mapping every real example under `packages/metalpython/extmod/wasmmod/examples/`
onto the schema above, to catch gaps before `convert-*` work starts. (`bridge`
already has its own decomposition sketch above — "No flat-dump exception".)

- **`hello`** → one node, `build = ["wasm"]`. **Converted** — see
  `examples/hello/`. Exports `hello`/`add` come from `PM_MOD_EXPORT_C`
  invocations in `__impl__.c`, not a manifest list. `[build_cfg]` carries
  today's `freeze = true` / `targets` / `keep_source = true` /
  `embed_source = true` / `compress_source = true` / `compress_pack = true`
  verbatim — nothing about those knobs changes, they just move under
  `[build_cfg]` — plus a new `mount = "mount"` (today's flat `src/` held
  both the C impl and the Python surface; the new tree's `src/` is
  fqn-anchored to native only, so the Python surface moved to a sibling
  `mount/` dir instead). `on_load`/`on_unload` both empty today, so both
  keys just get omitted (no more empty-string ceremony). See decision log.
- **`mixed`** → one node, `build = ["wasm"]`, `impl` split across a C child
  (`mixed_answer`) and an RS child (`mixed_i64`) per "one defining lang" —
  first real exercise of the slot-backed-wrapper + `PM_MOD_EXPORT_C`/`RS`
  same-artifact call shape end to end, not just the `ssh_connect` sketch.
- **`client`** → one node, `deps = { "pymergetic.wasmmod_examples.hello" =
  "0.1.0" }`, `on_load`/`on_unload` = `mp_pack_load`/`mp_pack_unload`. Its
  need for `hello.hello` becomes an ordinary import face, no `[[imports]]`.
  **Finding:** `client_elf/` exists as a hand-duplicated twin exactly like
  `ticks_elf/` — same merge opportunity, `build = ["wasm", "elf"]` on one
  node, not called out explicitly before this pass.
- **`ticks` + `ticks_elf`** → merge into one node, `build = ["wasm", "elf"]`
  (already sketched above). `elapsed`'s `sig = "i32"` is now derived from
  its `PM_MOD_EXPORT_C` call site instead of hand-typed. Its need for
  `micropython.runtime.ticks_ms` is a guest→host import face — no `deps`
  entry (see "Guest→host imports" above).
- **`host_elf`** (`pymergetic.wasmmod_examples.hostcall`) → one node,
  `build = ["elf"]` only. All 7 exports' `sig` tags (`i32`/`i32_i32`) derive
  cleanly from each `PM_MOD_EXPORT_C` call site's literal return/arg types —
  confirms the derivation approach on every real ELF sig tag that exists
  today, not just a hypothetical. All 10 of its imports (`wasmmod.host.*`,
  `wasmmod.mode`/`wasmmod.version`) are guest→host import faces, no `deps`.
- **`tree/*`** → **real finding, not just a relabeling.** The "flat sibling"
  layout (`tree/test_a_test_b_test_c/pack.toml`, one dashed directory per
  dotted node) fails the new schema outright: `fqn` is a checked assertion
  against the *real path*, and a flat dashed directory name doesn't spell
  the nested path `fqn` claims. Only the "nested" layout
  (`tree/nested/test_a2/test_b2/test_c2/`) is schema-compliant as-is — the
  flat layout needs real directory restructuring during conversion, not
  just a `pack.toml` → `__pmm__.toml` swap. A pure-namespace node with no
  content of its own (`test_b`/`test_b2`) follows the already-locked
  `wasmmod`-not-`pep420` precedent: `impl = "py"` with a trivial `__init__.py`,
  not `pep420 = true` — it has children but isn't open to outside
  contribution, so the namespace flag doesn't apply. `test_a.test_b.test_c`'s
  `deps` on `test_a.test_d` and matching import face carry over unchanged.
  **Second real finding:** once nested, `test_a/test_d/` physically sits
  inside `test_a/`'s own subtree while staying its own separate `build`
  root — the concrete, non-hypothetical case the "Recursive build boundary"
  rule exists for; `test_a`'s own packer run must stop at `test_d`'s `build`
  key, not fold its exports into `test_a`'s own artifact.

No gaps found beyond the three flagged above (`client`/`client_elf` merge,
`tree`'s flat-layout incompatibility, `tree`'s nested-build-root case) — the
schema as locked, plus the recursive-build boundary rule, covers every real
example in the reference tree.

---

## Open

None currently — the three items previously here (registration-codegen
reachability, ELF `sig` derivation, `version` scope) are all resolved; see
Decision log below.

## Decision log

| Date | Decision |
|------|----------|
| 2026-08-10 | `src/` only; path == module; one `impl` lang |
| 2026-08-10 | Faces = types / export / import; guard role, split edges |
| 2026-08-10 | Card = `*.pmm.toml` with `impl` only; name from path |
| 2026-08-10 | Retire `pack.toml`; one manifest system (`*.pmm.toml` everywhere) |
| 2026-08-10 | `on_load`/`on_unload`/`deps` move onto the card; dead fields (`type`/`comment`/`description`/`license`) dropped |
| 2026-08-10 | Deliverable root = card with `build` key (array — one tree, N container twins); build-only knobs nest under `[build_cfg]` |
| 2026-08-11 | `pmm-parser` impl found `build = [...]` + `[build]` (same key, array then table) is a real TOML parse error (`Cannot declare ('build',) twice`) — renamed the sub-table to `[build_cfg]` everywhere above. Also added `mount` to its documented keys (carries the old `python.mount` — an oversight when the other `python.*`/`[source]`/`[pack]` knobs were first moved under it): the mounted tree is pack payload data, not a node in the fqn/`impl` tree, so it's exempt from "one defining lang" — a `build_cfg.mount` dir can hold `.py` files even when the root card itself is `impl = "c"`/`"rs"`. |
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
| 2026-08-11 | Wasm engine: **WAMR**, vendored at `third_party/wamr` (nested git repo, not a top-level submodule — that's why an early search missed it). Registry's cross-container `Fn` convention (`pm_wasmmod_registry_value_t`, mirroring WAMR's own `wasm_val_t` kind+union shape directly rather than inventing a parallel encoding) and `pymergetic.wasmmod.loader` (new module: WAMR instantiation, export enumeration, per-export trampoline-adapter claiming) both land this session — see `REGISTRY.md` "Value convention" for the resolved design |
| 2026-08-11 | `build.rs` added (crate's first): compiles `pymergetic.util.mem`'s C impl + vendored TLSF via `cc`, and configures/builds WAMR's `vmlib` target via `cmake` (interpreter-only — no AOT/JIT, no WASI/libc-builtin; shared heap on). Neither was linked into `cargo build`/`cargo test` at all before this — `mem`'s own prior "verified via tests" sessions were checking the C code in isolation, not through this crate's own build |
| 2026-08-11 | WAMR build flags chosen deliberately minimal, not upstream's defaults: `WAMR_BUILD_AOT=0`/`WAMR_BUILD_JIT=0`/`WAMR_BUILD_FAST_JIT=0` (this milestone's fixture only needs the interpreter), `WAMR_BUILD_LIBC_BUILTIN=0`/`WAMR_BUILD_LIBC_WASI=0` (the fixture imports nothing host-side), `WAMR_BUILD_SHARED_HEAP=1` (the one feature this build exists for). Confirmed by a manual `cmake --build . --target vmlib` proof run before wiring `build.rs`, since getting WAMR building at all was a bigger unknown than anything downstream of it |
| 2026-08-11 | Shared-heap backing block: one `Box<[u8]>` (ordinary `alloc`, never freed — a once-per-process reservation) hands `pymergetic.util.mem` its arena's own backing bytes; the *shared-heap block itself* is then carved out of that arena via `pm_util_mem_alloc`, matching the plan's decision literally, rather than skipping the arena and just handing WAMR a raw `Box` pointer directly. Needed roughly `2×` the requested shared-heap size as arena backing (not `size + flat constant`) — same segmented-fit headroom lesson as `heap_grow_pool`'s own fix, this time hit empirically via the loader's own test rather than a dedicated `mem` adversarial case |
| 2026-08-11 | WAMR's `wasm_runtime_attach_shared_heap` hard-requires a module instance to already have a *default linear memory*, even a zero-page one — found by the loader's e2e proof failing `attach_shared_heap` on a fixture with no `(memory ...)` at all; the hand-assembled fixture now declares one empty (`min=0`) unexported memory purely so attach has something to overlap-check against, not because either exported function touches memory |
| 2026-08-11 | Adapter pool lives inside `pymergetic.wasmmod.loader` itself, not a separate `pymergetic.wasmmod.thunk` module — every one of the small fixed pool's stubs shares one identical body (`adapter_invoke`); a macro generates N distinct `extern "C" fn` *addresses* (the only thing that has to be distinct — plain fn pointers can't close over which exec_env/function a claimed slot means) rather than N distinct logic variants. `REGISTRY.md`'s existing "no thunk/trampoline generation yet" line still holds for a *generic*, engine-agnostic thunk story; this is WAMR-specific plumbing that belongs with the loader that owns WAMR |
| 2026-08-11 | `pm_wasmmod_loader_load`/`_unload` return/accept `pm_wasmmod_registry_handle_t` directly — no separate "loader handle" type. A loaded module *is* one registry entry; inventing a second handle concept to keep in sync with it would be exactly the kind of parallel source of truth this tree's "one source of truth" registry design exists to avoid |
| 2026-08-11 | First "wasm actually running" milestone proven end-to-end in `loader/__impl__.rs`'s own tests (`cargo test --lib`), not a separate fixture-file pipeline: a hand-assembled trivial `.wasm` (two exports, `answer: () -> i32` and `add_one: (i32) -> i32`) goes through `loader.init` → `loader.load` → `registry.resolve_native`/`pm_wasmmod_registry_call` → real WAMR call through a claimed adapter → `loader.unload` → confirmed-stale handle, entirely inside this crate, no wasm toolchain install required |
| 2026-08-11 | Found (via a genuinely flaky `cargo test --lib`, ~20% failure rate, not a one-off) and fixed a real cross-thread WAMR gotcha: `wasm_runtime_init()` sets up the hardware-bound-check signal/altstack env only for the *one OS thread* that called it; every other OS thread that later calls `wasm_runtime_call_wasm_a` — which is exactly the registry's whole point, "callable in any direction," not just from whichever thread happened to load a module — throws `"thread signal env not inited"`. Fixed by an unconditional `wasm_runtime_init_thread_env()` at the top of `adapter_invoke`, on every call, not just once per thread: it's a thread-local bool short-circuit on WAMR's side (`os_thread_signal_init`'s `if (thread_signal_inited) return 0;`), so the steady-state cost after a thread's first call is one cheap check, not a real re-init |
| 2026-08-11 | `WAMR_BUILD_AOT` flipped from `0` to `1` in `vmlib` — reversing the earlier "minimal on purpose" scoping now that the base loader/registry milestone is proven. Free at runtime for the plain-interpreter path and needs no LLVM there (confirmed: zero LLVM references anywhere under `core/iwasm/aot/`'s *runtime* side, only the separate `wamr-compiler`/`wamrc` *compile-time* tool touches LLVM) — also upstream's own default if left unset (`CMakeLists.txt:57-59`), so this un-does a deliberate divergence rather than adding a new one |
| 2026-08-11 | `wamrc` (WAMR's own AOT compiler CLI) is now built unconditionally by `build.rs`, against the **system** LLVM (`WAMR_BUILD_WITH_CUSTOM_LLVM=1` + `LLVM_DIR` from a located `llvm-config --cmakedir`), never WAMR's own from-source LLVM build (`wamr-compiler/build_llvm.py`, hours-long). `build.rs` searches `LLVM_CONFIG_PATH` env override, then `llvm-config`, then versioned names (`llvm-config-14`..`-20`) — first one that actually runs wins. Accepted, explicit trade-off: a machine without an LLVM dev package installed cannot build this crate at all, no feature gate. Exists purely so tests can compile real `.aot` fixtures (`wamrc -o out.aot in.wasm`) instead of hand-rolling AOT's compiled-native-code binary format the way the `.wasm` fixture is hand-rolled — that format genuinely isn't something to byte-for-byte by hand |
| 2026-08-11 | Real bug found wiring up `wamrc`'s build: the `cmake` crate derives its build tree as `{out_dir}/build` and *clears* that directory on reconfigure (`Config::maybe_clear`) — `build_wamr()`'s `vmlib` config and `build_wamrc()`'s `wamr-compiler` config (two entirely different CMake projects) both defaulted to the same bare `$OUT_DIR`, so configuring the second one wiped the first one's already-built `libiwasm.a`, surfacing as a genuine `could not find native static library iwasm` link error. Fixed by giving each `cmake::Config` its own `.out_dir("{OUT_DIR}/vmlib")` / `.out_dir("{OUT_DIR}/wamrc")` |
| 2026-08-11 | Attempted fast-jit (`WAMR_BUILD_FAST_JIT=1`) in the same pass and dropped it — WAMR 2.4.3's own `build-scripts/unsupported_combination.cmake` hard-rejects `WAMR_BUILD_SHARED_HEAP=1` + `WAMR_BUILD_FAST_JIT=1` together at configure time, and shared heap is load-bearing for this loader (the whole buffer/string-marshaling model), so fast-jit lost. Traced the actual root cause (upstream docs just say "currently not supported", no reason given): the shared-heap address-translation macro (`CHECK_SHARED_HEAP_OVERFLOW`/`app_addr_in_shared_heap`/`shared_heap_addr_app_to_native` in `core/iwasm/common/wasm_memory.h`) is used by both interpreters (`wasm_interp_fast.c`) and has a full LLVM-IR-level equivalent in AOT's own compiler (`compilation/aot_emit_memory.c`'s `setup_shared_heap_blocks`/chain-lookup) — but has **zero** matches anywhere under `core/iwasm/fast-jit/`. Fast-jit's own bounds-check codegen (`fast-jit/fe/jit_emit_memory.c`'s `check_and_seek_on_64bit_platform`) goes straight from "address out of linear-memory bounds" to an exception, with no branch to a shared-heap check at all — a genuine, real upstream gap (confirmed via `git log` on the constraint: PR #4690 added the *check*, not a fix), not something patchable from our side without porting that whole address-translation feature into WAMR's own proprietary JIT IR (new compiler-context fields + new IR-level control flow per memory-access opcode, replicated across a ~1200-line file) — real upstream-contribution-sized work, out of scope here |
| 2026-08-11 | Loader's container-kind detection: `wasm_runtime_get_file_package_type(bytes, len)` (mirrors `package_type_t`'s `Wasm_Module_Bytecode`/`Wasm_Module_AoT`) runs on the raw bytes *before* `wasm_runtime_load`, replacing the hardcoded `pm_wasmmod_registry_container_kind_t::Wasm` that `pm_wasmmod_loader_load` previously always passed to `publish` — the registry's already-reserved `Aot` enum slot (`registry/__types__.h`) is finally driven by something real. Nothing else in the load path branches on this; `wasm_runtime_load`/`instantiate`/`call_wasm_a` are identical calls for both kinds, WAMR dispatches internally |
| 2026-08-11 | AOT proof deliberately reuses the existing `.wasm` fixture bytes rather than a second hand-written binary: `compile_to_aot()` in the loader's own tests shells out to the `wamrc` binary `build.rs` already built (`env!("WAMRC_PATH")`), writing/reading real temp files, and hard-asserts on failure (a real `wamrc` failure here is a regression worth seeing loudly, not a "skip and hope" case) — same `answer`/`add_one` assertions as the interpreter path, proving the `Value` convention is genuinely container-kind-agnostic from the caller's side |
| 2026-08-11 | ELF `sig` tag derivation resolved: parsed straight from the literal C-type argument already passed to `PM_MOD_EXPORT_C`/`PM_MOD_EXPORT_RS` at each call site (e.g. `int(int, const ssh_opts_t *)`), never a manifest field — same `i32`/`i32_i32`/... tag vocabulary the old `pack.py`'s `sig_tag()` used, just sourced from real code instead of a hand-typed string; keeps ELF consistent with "exports come from faces, not lists" |
| 2026-08-11 | `version` is usable on any module, not just `build`-marked roots — `deps` on a non-root module is meaningful too, not just whole-package-depends-on-whole-package |
| 2026-08-11 | **Corrected the row above** — reverted `version`/`deps` back to `build`-marked-roots-only, which is what the doc's own earlier "No `pack.toml`" table (`[deps]` "only meaningful where `build` is set") and "Deliverable root" example (`version` "only meaningful for a root") already said before the row above contradicted them without noticing. The underlying reasoning: a non-root module never exists independently of whichever root's artifact contains it (same commit, same checkout, always built together) — there's no real sense in which it could be "at a different version" than its own siblings, so `version` genuinely is a container-level concept, not a module-level one; `deps` follows the same restriction because its only legal target is something with a `version` to pin |
| 2026-08-11 | Recursive build boundary: a `build`-marked root's own subtree can contain another `build`-marked root (confirmed by a real case, not a hypothetical — `tree/test_a_test_d/` becomes a real nested `test_a/test_d/` directory once paths must match `fqn`, while staying its own independent `.wasm` artifact). Rule: the outer root's packer run stops descending the instant it hits a nested `build` key — that subtree belongs entirely to its own build, never folded into the outer root's compiled output; any relationship between them is an ordinary cross-artifact `deps`/import-face pair, identical in shape to depending on a root that isn't nested under you at all |
| 2026-08-11 | Two `wasmmod` trees now exist side by side: `packages/metalpython/extmod/wasmmod/` (old, pinned to `metalpython`'s `master`, still has the real `pack.toml` packer + every example package + `run_matrix.py`/`run_elf.py`/`Makefile` — read-only reference, port FROM) vs. `packages/metalpython-wasmmod/extmod/wasmmod/` (this tree, blank-main rewrite — destination, port TO); worth writing down once since it's easy to grab the wrong one |
| 2026-08-11 | Consolidated the `*.pmm.toml` schema (card keys, face files per `impl`, guest→host-import-has-no-`deps` rule) into one flat reference table instead of leaving it derivable only from scattered rationale prose; dry-ran it against every real example package in the old reference tree and found exactly two real gaps: `client`/`client_elf` is the same hand-duplicated-twin merge case as `ticks`/`ticks_elf` (not previously called out), and `tree/`'s "flat sibling dashed-directory" layout fails the new `fqn`-must-match-real-path assertion outright — only its "nested directory" twin layout is schema-compliant as-is, so that conversion needs real restructuring, not just a manifest swap |
| 2026-08-11 | `pmm-parser` shipped (`dev/tools/src/pymergetic/wasmmod/tools/pmm.py` + `faces.py`, C-only v1): walks a `*.pmm.toml` card tree, enforces fqn/path + recursive-build-boundary + `deps`/`version`-root-only at load time, then *synthesizes a `pack.toml`-shaped dict* rather than reimplementing `manifest_to_build()` — the existing, untouched function is still the one place a manifest dict becomes a build plan, for either manifest format. `pack.py`'s `main()` gained one dispatch branch (`pmm.resolve_pmm_root()` tried first, `resolve_pack_root()` unchanged as fallback); both formats coexist, `pack.toml` support is untouched (`remove-pack-toml` stays a separate, later backlog item) |
| 2026-08-11 | Face-export-discovery v1 (C only): regex-scans `__impl__.c` for `PM_MOD_EXPORT_C(module, export_name, impl_fn, c_type_signature)` call sites, parses the literal signature text into the same compact `i32`/`i32_i32`/… tag `pack.py`'s own `sig_tag()` already understood (anything not all-i32 — floats, 64-bit ints, structs by value — omits `sig` entirely and falls back to `SIG_AUTO`, which the loader's real Wasm-type introspection always handles correctly anyway, just not compactly). `PM_MOD_EXPORT_C` itself lives in the unified tree as `src/pymergetic/wasmmod/guest.h` (umbrella, path == module — **not** a parallel `include/`); it is a deliberate no-op at the C level; real slot-backed-wrapper + eager-connect registration is separate, later `pm-mod-export-macro` work, not needed yet since `hello` has no same-artifact private calls |
| 2026-08-12 | mpwm host face restored at `ports/micropython/` (thin `modwasmmod.c` + `upy-host` staticlib): auto `install_hook`, `sys.modules` → `pm_wasmmod_registry_publish` presence sync, `load`/`call` over Rust registry+loader. No `host_slots`/`call0_py`. Pack finder (`loader/finder/`) still next |
| 2026-08-12 | Removed the mistaken parallel `include/` and `python/` top-level dirs: guest ABI is `src/pymergetic/wasmmod/guest.h`, PyPI `rt` is `src/pymergetic/wasmmod/rt/`, packaging manifest is root `pyproject.toml` (sibling of `Cargo.toml`, not a second source tree). Packer's `-I` was crate root so guests `#include "src/pymergetic/wasmmod/guest.h"` (superseded 2026-08-13) |
| 2026-08-13 | **-I rule tightened:** `#include` never spells `src/` — use `-Isrc` + `#include "pymergetic/…"`. Packer `guest_include_flags()`, `.clangd`, `micropython.mk`, `build.rs`, CDBs updated. Files still live under `src/` on disk. |
| 2026-08-13 | **`wasmmod-gen` py + guest access faces:** `impl=py` → in-crate scan of `__init__.py` hints → `register_fn` → emit `__exports__.h/.rs`. Unlinked C cards (guest `hello`) → scan `PM_MOD_EXPORT_C` in `__impl__.c` into the same registry emit path. Empty umbrella `pymergetic.wasmmod` still skips. |
| 2026-08-13 | Dropped host `python3`/`ast` blob from `util.gen` discover — pure Rust string scan (same spirit as `PM_MOD_EXPORT_C`). |
| 2026-08-13 | **Live `pm_wasmmod_pyexport_*`:** pool thunks + `bind_py`; `bytes→int` faces use bufptr (discover SoT). |
| 2026-08-13 | **Cookie mem / handle obj:** host `memcookie` + `objhandle` tables; pyexport pools; discover `"mem"`/`"obj"`; `pysample.echo_mem` / `is_none`. |
| 2026-08-14 | **Discover arity/scalars:** 2–3×`int` + hint aliases `i64`/`int64`, `f32`/`float`, `f64`/`float64`; nativecall arms match. |
| 2026-08-14 | **CPython port:** `ports/cpython/` wraps `ports/common/` + objhandle twin + import→ready (`pyexport`/`hostready`/`nativecall`/`importhook`); `make smoke` proves pysample typed call. Portable `pm_wasmmod_py_obj_t` in `host/pyobj.h`. |
| 2026-08-14 | **`pymergetic.wasmmod.io` restored** (before CDN client): `io_ops` table (`fetch`/`probe`/`yield`/`request`) + POSIX file + optional `http://` GET/HEAD. Wait class locked on faces (`async`/`facade`/`sync`); mpwm may block inside the fill; Metal later parks in the same slot. No mbedtls this slice (`https://` → host ops / later TLS). Builtin ops DECLINE so the default chain runs. Compiled only in `build.rs` (`static:+whole-archive`); not duplicated in `micropython.mk` / CPython `CORE_SRCS`. Boot calls `pm_wasmmod_io_set(NULL)`. |
| 2026-08-14 | **Finder HTTP candidates on `io`:** µPy `ports/micropython/finder.c` probes HTTP roots on `wasm.path`/`sys.path` via `pm_wasmmod_io_probe` + `join_uri`, loads hits via `pm_wasmmod_io_fetch` (copy onto GC heap). Local roots stay on µPy VFS (`mp_import_stat` / `mp_vfs_open`) — not POSIX `fopen`. Same container forms as offline (`.elf`/`.aotN`/`.wasm` + `.zlib`). |
| 2026-08-14 | **`pymergetic.wasmmod.net.cdn`:** metal-cdn client on `io_ops` (not a net stack). Bases + bearer + `artifacts/lead|pin/{name}{ext}` + `index/{channel}`. Rejects non-artifact 200s. Finder skips configured bases as flat HTTP roots; `import_pack` / dep load uses `fetch_pack` when driver is metal. µPy `wasm.cdn` / `cdn_prepend` / `cdn_reset` / `catalog` / `session_id` / `publish` / `publish_file` (qstrs in `qstrdefs.wasmmod`). C `publish` is still a stub (`io.request` unused). `https://` TLS stays later. |
| 2026-08-14 | **CPython pack finder / pack_bind / ELF:** `ports/cpython/finder.c` twins µPy (POSIX local + `io` HTTP + metal-cdn `fetch_pack`). `packbind.c` prefers `.cpy.`/`.py`, rejects `.upy.`/`.mpy`. ELF64 ET_REL loader lives at `src/pymergetic/wasmmod/pack/format/elf/load.c` (µPy `micropython.mk` already listed it). `load`/`unload`/`path`/`cdn`/`catalog` on the CPython face. `publish` still a stub. |
| 2026-08-14 | **CPython `util.gen` VFS:** `ports/cpython/modgen.c` fills `VfsSink` ops with POSIX fopen (no µPy VFS). `pymergetic.util.gen.run_vfs` / `.diff` + live pyi provider (import fqn, walk callables). `wasmmod.gen` / `.run` still `pm_util_gen_run` (needs cargo `gen`; returns -1 on the `upy-host` staticlib). |
| 2026-08-14 | **`io.request` + CDN publish:** public `pm_wasmmod_io_request` (ops short-circuit / DECLINE / native `http://` POST-PUT with 2xx; `https://` still needs host ops). `pm_wasmmod_net_cdn_publish` POSTs original bytes to `{base}/artifacts/lead|pin/...` via `io.request` (not sockets; not the metal-cdn-client multipart JSON API). Python `publish` / `publish_file` raise `NotImplementedError` only for “not supported” / “https requires”; other failures are `OSError`. |
| 2026-08-14 | **Default `https://` in `io` fill:** native HTTP wraps the fd in µPy-vendored mbedtls SSL (`MICROPY_SSL_MBEDTLS`, `VERIFY_NONE` like micropython-lib requests). Finder/CDN still only call `io.fetch`/`probe`/`request`. Host `io_ops` still wins (Metal/browser). Cargo compiles `lib/mbedtls` into `pm_mbedtls` only with `bundle-mbedtls` (cargo test / CPython). µPy unix already compiles those sources — `--no-default-features`, no second copy, no `--allow-multiple-definition`. |
| 2026-08-14 | **`ports/metal/` stub:** `io_ops.c` weak `pm_metal_wasm_io_fetch`/`_probe`/`_request` DECLINE + `pm_metal_async_yield`; runtime `pm_wasmmod_metal_io_ops_init` (no const fn-ptrs on UEFI PE). `mpconfig_metal.h` (`MICROPY_WASM_IO_OPS` / TLSF malloc comments, GC+scheduler off). `wamr_freestanding.mk` wasmmod-owned (interp + shared heap, AOT/JIT off; Metal supplies platform glue). Not a TLS module, not `register_upy`. Not added to `SRC_WASMMOD` / CPython `CORE_SRCS` / `build.rs`. |
| 2026-08-14 | **`extmod/metal`:** `pymergetic.metal` package shell (`MICROPY_PY_METAL=1` needs wasm). Heap ABI is `pymergetic.util.mem` only — no `pm_metal_mem_*`, no `pymergetic.metal.mem` Python holder. |
| 2026-08-15 | **`pymergetic.metal.async`:** stackless coro/task, lock-free ready ring, sorted timer list, `run_until` nested drain + idle wait. Strong `pm_metal_async_yield`. Weak `pm_metal_net_ip_pump`. Host prove `make -C extmod/metal test`. Not Asyncify. Not `run.c`/`coro.c`. |
| 2026-08-15 | **`pymergetic.metal.net.ip`:** IPv4 + ICMP echo + UDP + TCP + lo; L2 ops attach; strong `pm_metal_net_ip_pump`. TCP is lo-reliable (handshake/data/FIN, no rexmit). UDP/TCP empty recv parks the current task. |
| 2026-08-15 | **`pymergetic.metal.net.tls`:** same µPy-vendored mbedtls on ip socks (client `VERIFY_NONE`, server DER cert/key). Park handshake/send/recv like ip. Host prove links `lib/mbedtls`; unix `MICROPY_PY_METAL=1` does not compile mbedtls again. |
| 2026-08-15 | **`pymergetic.metal.net.http`:** GET/HEAD/POST on ip TCP; strong `pm_metal_wasm_io_fetch|_probe|_request` park via `run_until`. IPv4 literals only. `https://` via `net.tls`. Host prove `make -C extmod/metal test`. |
| 2026-08-15 | **`pymergetic.metal.drivers`:** closed product HW namespace (not pep420 — that test is “can another distribution add a child”, same as `wasmmod/`). L2 NICs are `pymergetic.metal.drivers.net.{tap,virtio,bge,sim}` (`impl = c`, C ABI `pm_metal_drivers_net_*`). `metal.net` stays the stack; `wg` stays `pymergetic.metal.net.wg` (tunnel, not a NIC). gfx/audio later under `metal.drivers`. Rust barrels `metal/drivers.rs` + `metal/drivers/net.rs`. |
| 2026-08-13 | **No `__init__.pyi` beside `__init__.py`:** `impl=py` and pack `mount/` keep typing in the `.py` (`TYPE_CHECKING` / real hints). `__init__.pyi` is only the generated editor face for `impl=c`/`rs` (no `.py` muscle). `wasmmod-gen` skips/scrubs sibling `.pyi` for `impl=py`. |
| 2026-08-13 | **Module versioning (complete):** `ModEntry.version` + `publish_ver`/`set_version`/`version` query; always-on `wasmmod.pkg` (MPPK) baked on pack and into registry on load/ELF; `pymergetic.util.version` (`cmp`/`satisfies`: `*`, exact, `>=`, `^`, PEP440 compact pre like `0.2.0a2`); finder enforces dep pins against registry (missing version = hard fail); card `version` → gen `__version__.h` → `MICROPY_WASM_VERSION` / registry seed; `wasmmod.version` reads registry. `deps` stay `build`-root-only; `version` also allowed on host/kernel publish units (no `build`) — `pmm.py` gate updated. |
| 2026-08-13 | **Module `__tests__.*` standard:** cases live in `__tests__.{rs,c,py}` (optional `__tests__/` split), register via `PM_MOD_TEST_*` into `ModEntry.tests` (not product exports / not facegen). Host harness `util::mod_test::registry_mod_tests_all`. Guest: packer compiles `__tests__.c`, emits `wasmmod.tests` (MPTE); loader registers test trampolines. In-bin: `wasmmod.test` / `test_all` / `tests` / `test_count`. Migrated version/lock/lz4/mtar/api/registry/loader/gen off `__impl__` blobs. |
| 2026-08-11 | Found while implementing `pmm-parser`: `wasmmod_root()`'s own `_looks_like()` (`dev/tools/src/…/paths.py`) only recognized the *old* reference tree's shape (`loader.c` / `crates/wasmmod-read`) — a real, previously-unnoticed bug, since every packer path (`guest_include_dir()`, `find_wasm_ld()`, `find_clang()`, `find_mpy_cross()`) depends on it to find the guest umbrella header and friends. Broadened to also recognize this tree's shape (`Cargo.toml` + `src/pymergetic/wasmmod/`) |
| 2026-08-11 | `hello` converted for real (`examples/hello/`, first `examples/` dir in this tree) and proven two ways: manually via `make -C examples/hello` (built a real, loadable `.wasm`) and via a new Rust test (`load_call_unload_roundtrips_through_real_pmm_pack` in `loader/__impl__.rs`) that shells out to the packer for real, then loads the result through `pm_wasmmod_loader_load` and calls `hello()`/`add(41,1)` via the registry — same "real, not synthetic" posture as the AOT proof. Mount-tree placement (see the `[build_cfg].mount` decision row above) meant restructuring `hello`'s old flat `src/` (native + Python side by side) into `src/pymergetic/wasmmod_examples/hello/` (fqn-anchored, native only) plus a sibling `mount/` (the old Python tree, byte-for-byte, as payload data) — a real, necessary shape change beyond a pure manifest-format swap, driven by "path == module" now actually being enforced |
