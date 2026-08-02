# MicroPython WASM pack format

A **pack** is one `.wasm` deliverable: mixed native code (C / C++ / Rust)
linked into a single Wasm module, plus an optional multi-level Python tree
(`.py` / `.mpy`) embedded in a custom section. At load time the pack is
registered into normal MicroPython `sys.modules` so `import pkg.sub` works
like any other package.

Today’s loader (`extmod/wasmmod/wasmmod.c`) only instantiates Wasm and offers
`call()`. This document is the **target design** for packing, registration,
and host↔guest / guest↔guest calls. Bits marked **v1 (shipped)** exist in
`tools/wasm_pack.py` now; the rest is proposed structure.

## Goals

- One file = one importable package
- **Native is compiled** (many `.c` / `.cc` / `.rs` → one Wasm); **Python is a tree** (many files, nested packages)
- Register with **`sys.modules`** (and a real package object), not a parallel registry
- Call **host → guest**, **guest → host**, **guest → guest**
- Explicit **load / unload** lifecycle
- Upstream-friendly: optional port hooks only; no external-bus types in the pack format

## Big picture

```text
                         host MicroPython
                    (sys.modules, import, call)
                              │
              load/unload │   │ attribute / call
                          ▼   ▼
                 ┌────────────────────┐
                 │  pack instance     │
                 │  (one Wasm module) │
                 └─────────┬──────────┘
            native exports │  embedded py/mpy
                          ▼
              C / C++ / Rust  +  pkg/**/*.py
```

Call directions:

| Direction | Mechanism |
|-----------|-----------|
| **Python → native (guest)** | Bound accessors on the package (trampolines into Wasm exports) |
| **Native → Python / host** | Wasm `import`s implemented by the loader (host natives) |
| **Guest → guest** | Declared imports `(module, func)`; loader installs forwarders at instantiate |
| **Host → guest (non-Python)** | Same export table; optional `EXPORT_PUBLISH` port hook |

## Source layout (one pack)

Python may be arbitrarily nested. Native sources are **not** mirrored as
import paths — they compile and link into **one** Wasm module; only the
**export table** names what shows up in Python.

```text
mypkg/                          # pack root
  pack.toml                     # manifest (TOML)
  native/                       # compiled together → one .wasm
    foo.c
    bar.rs
    util.cpp                    # OK; see C ABI note below
  py/                           # mounted into micropython.pack (multi-level)
    __init__.py
    util.py
    sub/
      __init__.py
      mod.py
```

**C ABI at the boundary.** C++ and Rust are supported as *implementation*
languages inside the pack, but anything published across Wasm (exports in
`[[exports]]`, lifecycle `mp_pack_load` / `mp_pack_unload`, and guest imports)
must use a **C ABI**: `extern "C"` (no C++ mangling), Wasm-friendly types
(primarily `i32` / pointers-as-`i32` offsets for v1–v2). C++ classes, Rust
traits, etc. stay private to the guest; wrap them behind C-callable stubs
before exporting.

Build result: **`mypkg.wasm`** (single file).

```text
native/*.{c,cc,rs}  ── compile + wasm-ld ──┐
                                           ├──► mypkg.wasm
py/**               ── embed as section ───┘
```

Analogy: think of the pack as a MicroPython **extension module** (CPython’s
`.pyd` / `.so` idea), except the native part is Wasm and the pure-Python
parts travel inside the same file instead of beside it on a filesystem.

## Manifest: `pack.toml`

Human-facing description of the pack. The tool reads it; selected
fields are baked into `micropython.pack` / `micropython.imports`. TOML for
lists/comments.

### Shape (proposed)

```toml
# Required
type = "package"              # wasm pack kind
name = "mypkg"                # import root / sys.modules key
version = "0.1.0"

# Optional blurb (tooling/docs only; not required in the wasm)
comment = "Mixed native + Python demo pack"

# Languages present under native/ (mixed allowed).
# Tools use this to pick compilers; empty = discover by file extension.
# "cpp" / "rs" are impl languages only — exported surface is still C ABI.
impl = ["c", "rs"]            # subset of: "c" | "cpp" | "rs"

[python]
mount = "py"                  # directory tree → micropython.pack files[]
keep_source = true            # keep .py beside bytecode (recommended)
# freeze = true               # opt-in bytecode (default source-only)
# targets = [                 # host-tagged names in the pack section
#   "upy:mpy6:sib31",
#   "upy:mpy6:sib63",
#   "cpy:cp312",
# ]

[native]
dir = "native"                # default; all sources linked into one wasm
# sources = ["native/foo.c"]  # optional explicit list (else glob dir)

[lifecycle]
load = "mp_pack_load"         # wasm export name, or "" to omit
unload = "mp_pack_unload"

# Published into sys.modules as accessors (→ micropython.pack exports[] v2)
[[exports]]
module = ""                   # "" = package root (mypkg.add)
func = "add"
export = "add"                # wasm export; default = func
sig = "i32_i32_i32"           # see signature tags

[[exports]]
module = "sub"
func = "ping"
export = "sub_ping"
sig = "i32"

# Guest→guest needs (→ micropython.imports)
[[imports]]
module = "greeter"            # other pack's `name`
func = "hello"
```

### Minimal manifest

```toml
type = "package"
name = "hello"
version = "0.1.0"
impl = ["c"]

[python]
mount = "py"

[[exports]]
func = "hello"
sig = "i32"

[[exports]]
func = "add"
sig = "i32_i32_i32"
```

(`module` / `export` default to `""` / `func` when omitted.)

## Loader: `sys.modules` and natural imports

The loader’s job is to make a pack **behave like a normal MicroPython
package**. After load, ordinary `import` / `from … import` must work for
both embedded Python children and bound native exports — including the
case where the “root” is native (C/Rust Wasm) and children are `.py`.

### Registration steps

On successful load of a pack named `mypkg`:

1. Fetch bytes (VFS path, HTTP URL, or already in memory) → optional
   **signature verify** (see below).
2. Instantiate Wasm (resolve `micropython.imports` / host imports).
3. Call optional `mp_pack_load` (lifecycle).
4. Create / reuse the root module object; store in **`sys.modules["mypkg"]`**.
5. Publish embedded Python tree under that root (virtual filesystem +
   import hook, or eager `exec` into submodule objects):
   - `py/util.py` → `sys.modules["mypkg.util"]`
   - `py/sub/__init__.py` → package `mypkg.sub`
   - `py/sub/mod.py` → `sys.modules["mypkg.sub.mod"]`
6. Bind `[[exports]]` accessors onto the correct module objects
   (`mypkg.add`, `mypkg.sub.ping`, …).
7. Set pack metadata on the root (`__file__`, `__wasm__`, `__path__` as
   needed) so relative imports inside embedded `.py` resolve naturally.

Unload (explicit `wasm.unload` / `close`):

1. `mp_pack_unload` if present.
2. Delete `mypkg` and every `mypkg.*` key from `sys.modules`.
3. Tear down Wasm instance + trampolines; invalidate guest→guest caches.

### Natural import behaviour

Once registered, **no special syntax** is required for in-pack use:

```python
import mypkg              # root (native accessors + __init__.py if any)
import mypkg.util         # py child of a C/RS pack — normal
from mypkg.sub import mod
mypkg.add(2, 3)           # native export accessor
```

Embedded `.py` may use relative imports (`from .util import x`). The loader
must make submodule lookup prefer the pack’s virtual tree before falling
through to the real VFS, so a child name does not accidentally resolve to
an unrelated file on disk.

Native code is **not** a separate importable `.py` path; it only appears as
attributes (and lifecycle) on modules that already exist from the pack
name / Python tree / export `module=` field.

### On-disk / URL naming: `__init__.wasm` vs `foo.wasm`

Mirror Python’s package vs module layout so discovery feels obvious:

| Artifact | Meaning | `sys.modules` root |
|----------|---------|---------------------|
| `mypkg.wasm` / `mypkg.aot` | **Module pack** (interp / AOT; name from manifest or stem) | `mypkg` |
| `mypkg/__init__.wasm` / `.aot` | **Package pack** (directory is the package) | `mypkg` |
| `mypkg/sub.wasm` / `.aot` | Child pack under package `mypkg` | `mypkg.sub` (nested pack) |
| `mypkg/sub/__init__.wasm` / `.aot` | Nested package pack | `mypkg.sub` |

Prefer `.aot` over `.wasm` when `MICROPY_PY_WASM_AOT` is enabled (see AOT).

Rules:

- Manifest `name` **wins** when present; otherwise derive from path
  (`foo/bar/__init__.wasm` → `foo.bar`, `foo/bar.wasm` → `foo.bar`).
- A directory that is a Wasm package may still contain loose `.py` beside
  `__init__.wasm` for development; release packs should embed Python in
  the section so one file stays enough.
- Child `.wasm` files under a package dir are found by `import_wasm` /
  path search the same way `import` finds child `.py` — see below.

```text
lib/
  hello.wasm                 # module pack → import hello
  sensor/
    __init__.wasm            # package pack → import sensor
    calibrate.py             # optional sidecar during dev
    drivers/
      __init__.wasm          # → import sensor.drivers
```

## `import` vs `import_wasm` vs import hook

MicroPython does **not** ship CPython’s `sys.meta_path` / `PathFinder`.
Builtin `import` walks **`sys.path`** itself (`py/builtinimport.c`:
stat `.py` / `.mpy` / directories). So “add a finder” here means either teaching
that path walk about `.wasm`, or wrapping `__import__`.

| API | Sees |
|-----|------|
| **`import name`** | Frozen / VFS `.py`·`.mpy`, plus anything already in `sys.modules`. With hook installed (below), also auto-loads packs. |
| **`wasm.import_wasm(name)`** | Always Wasm-aware search + load-or-reuse; no hook required. |
| **`wasm.install_hook()`** | Opt-in: make builtin `import` use the Wasm path finder too. |

### Shared path finder (one implementation)

Both `import_wasm` and the hook call the same finder:

```text
find_wasm(fullname_name) → path_or_url | None

for root in wasm.path:          # may overlap / extend sys.path
  try root/name/__init__.wasm   # package pack
  try root/name.wasm            # module pack
  # dotted name → nested dirs: sensor.drivers → sensor/drivers/…
```

So yes: **path-based finder**, same mental model as `sys.path`, just also
understands `__init__.wasm` / `name.wasm` (and optional HTTP roots).

### `wasm.import_wasm` (always available when `MICROPY_PY_WASM`)

```python
import wasm

m = wasm.import_wasm("sensor")
# 1. sys.modules["sensor"] if already pack-owned
# 2. finder on wasm.path (and optionally sys.path)
# 3. load_pack → register → return module

d = wasm.import_wasm("sensor.drivers")  # parent first if needed
wasm.unload("sensor")
```

### `wasm.install_hook()` / `uninstall_hook()`

Opt-in so normal `import sensor` hits packs without calling `import_wasm`:

```python
import wasm
wasm.path = ["/lib/wasm", "https://example.org/packs/"]
wasm.install_hook()     # wrap builtins.__import__ (or gated C hook)

import sensor           # finder runs; loads sensor/__init__.wasm if needed
import sensor.drivers
from sensor import util # py child inside the pack — already natural

wasm.uninstall_hook()   # restore previous __import__
```

Implementation options (pick smallest upstreamable one):

| Approach | How | Pros |
|----------|-----|------|
| **A. Wrap `__import__`** | Pure Python/C in `wasmmod.c`: save old, new calls finder then old | No `py/` core change; easy PR |
| **B. Extend path walk** | `#if MICROPY_PY_WASM` in `builtinimport.c` next to `.py`/`.mpy` stat | Fast, “real” finder on `sys.path` |
| **C. Both** | B when `MICROPY_PY_WASM_IMPORT=1` at build time; A via `install_hook()` for runtime toggle | Best of both |

Recommendation: implement the **finder once**, expose it as `import_wasm`,
default **A** (`install_hook`) for opt-in; optional **B** behind a config
flag so ports that want transparent `import` pay for it explicitly.

Hook must be careful to:

- Only intercept when the name is not already satisfied by frozen/builtin
  (delegate to previous `__import__` first, or only on `ImportError`)
- Prefer “try previous import, on failure try Wasm” to avoid stealing
  stdlib names unless a pack is intentionally on `wasm.path` first
- Re-entrancy: loading a pack that `import`s `.py` children must not
  recurse forever (children come from the pack VFS, not the hook)

## AOT (in addition to interpreter Wasm)

WAMR can run guests in the **interpreter** (what we link today) or as
**AOT** native code produced offline by `wamrc`. AOT is typically much
faster and is worth first-class support next to `.wasm`, not a fork of the
pack format.

### Build / runtime flags

| Piece | Role |
|-------|------|
| `MICROPY_PY_WASM` | feature on; interp sufficient |
| `MICROPY_PY_WASM_AOT` | also build WAMR with AOT (`WAMR_BUILD_AOT=1`) and accept `.aot` |
| Host tool `wamrc` | compile `foo.wasm` → `foo.aot` for a target triple / CPU |

Unix smoke can stay interp-only. Ports that care about speed enable AOT and
ship prebuilt `.aot` (MCU/EFI cannot usefully run `wamrc` on-device).

### Artifacts and finder order

Same naming rules as Wasm, with AOT preferred when present:

| Package | Try first | Fallback |
|---------|-----------|----------|
| module | `name.aot` | `name.wasm` |
| package | `name/__init__.aot` | `name/__init__.wasm` |

`import_wasm` / the path finder use that order. One pack identity (`name` in
the manifest) — execution engine is an implementation detail.

### Pack metadata vs AOT bytes

`micropython.pack` / `micropython.imports` live in **Wasm custom sections**.
`wamrc` consumes `.wasm` and emits `.aot`; custom sections are not a reliable
place to read metadata from the `.aot` alone.

Recommended shipping layouts:

1. **Pair (simple):** `hello.wasm` (or stripped meta-only) + `hello.aot`  
   Loader reads sections from `.wasm`, executes `.aot` when
   `MICROPY_PY_WASM_AOT` and load succeeds.
2. **Sidecar:** `hello.aot` + `hello.mpack` (raw `micropython.pack` payload
   bytes extracted at pack time). No need to keep full Wasm on the device.
3. **Wasm-only:** interp path (today).

Tooling:

```sh
python3 tools/wasm_pack.py examples/hello/ -o hello.wasm
wamrc -o hello.aot hello.wasm          # target-specific flags as needed
# optional: wasm_pack --write-mpack hello.mpack
```

Signature verify applies to the **bytes that will be instantiated** (the
`.aot` or `.wasm` actually loaded), and should also cover metadata
(sidecar hash / same signed envelope).

### Loader behaviour

```text
find artifact → verify →
  if .aot and AOT enabled: wasm_runtime_load_aot(...)
  else:                    wasm_runtime_load(wasm)   # interp
→ instantiate → sys.modules publish (unchanged)
```

Guest↔guest forwarders, lifecycle exports, and Python accessors stay the
same — only the code engine behind the export changes.

### Upstream notes

- Default `MICROPY_PY_WASM_AOT=0` (keeps default link smaller).
- Document that `.aot` is **CPU/OS-specific**; `.wasm` stays portable.
- Add `wamrc` as an optional host dependency in docs, not a submodule
  requirement for interp-only builds.

## Fetch: VFS and HTTP

| Source | How |
|--------|-----|
| **VFS** | `open(path, "rb")` / `mp_reader` — same as today’s `wasm.load(path)` |
| **memory** | `wasm.load(bytes)` |
| **HTTP(S)** | `wasm.load("https://…/pkg.wasm")` or search roots that are URL prefixes; use existing `requests` / socket stack when present |

Search path (proposed):

```python
wasm.path = ["/lib/wasm", "https://example.org/packs/"]
wasm.import_wasm("hello")   # tries each root
```

Ports may replace the fetcher with a hook (`MICROPY_WASM_FETCH(url) -> bytes`)
to supply a custom HTTP / VFS stack without forking the loader.

## Signature verification (mbedtls)

When `MICROPY_SSL_MBEDTLS` (or a smaller dedicated crypto switch) is
enabled, packs may be distributed as **signed blobs**. Goal: verify
*before* instantiate.

### Proposed distribution shapes

1. **Detached signature** (simplest for VFS):
   - `hello.wasm`
   - `hello.wasm.sig` (raw signature bytes) + trust store of public keys
2. **Envelope** (better for HTTP):
   - custom section `micropython.sig` on the wasm, or
   - outer container: `magic | pubkey_id | sig | wasm_bytes`

Algorithm (initial): **Ed25519** or **ECDSA-P256** via mbedtls — pick one
in implementation; document in pack.toml:

```toml
[trust]
# optional; tool may detach-sign at pack time
# sign = true
```

Loader policy (config):

| Mode | Behaviour |
|------|-----------|
| `MICROPY_WASM_VERIFY=0` | no verify (default for unix smoke) |
| `MICROPY_WASM_VERIFY=1` | require valid sig for every load |
| `MICROPY_WASM_VERIFY=2` | verify if `.sig` / section present; else allow |

Failed verify → do not instantiate, raise `OSError` / `ValueError`. Public
keys come from a port-supplied store (flash partition, `wasm.add_trust(key)`,
or compile-time pinned keys) — not from the pack itself.

Ports can plug a custom verify hook; the default path uses mbedtls + a
key ring API (`wasm.add_trust`).

## Export table (module + func)

The public surface for MicroPython is **`sys.modules` + attributes**.

Each published native symbol has:

| Field | Meaning |
|-------|---------|
| `module` | Dotted name under the pack root (`""` / `"."` = package root) |
| `func` | Attribute name on that module |
| `export` | Wasm export name (may equal `func`) |
| `sig` | Compact signature for the binder (v2+) |

Example:

```text
module=""     func="add"    export="add"     # mypkg.add
module="sub"  func="ping"   export="sub_ping" # mypkg.sub.ping
```

Accessors wrap Wasm calls so Python sees ordinary callables. Under the hood
the loader binds a small **trampoline** into each export. Numeric
ABI today is i32 / i64 / f32 / f64 (any arity; multi-result → tuple). The
stable handle for Python is the bound callable on the module.

## Lifecycle exports (convention)

Optional Wasm exports (linker names fixed):

| Export | Sig (proposed) | When |
|--------|----------------|------|
| `mp_pack_load` | `() -> i32` | After instantiate, before `sys.modules` publish; `0` = ok |
| `mp_pack_unload` | `() -> i32` | Before tear-down; must not call back into dying peers |

If absent, load/unload still work; these are for guest constructors /
resource hooks.

## Call graph

```text
┌──────── host (upy) ────────┐
│  sys.modules["mypkg"]      │
│  mypkg.add(...)            │── accessor / trampoline ──► Wasm export add
│  host_fs_read(...)         │◄── Wasm import ─────────── guest native
└────────────────────────────┘

┌──────── guest A ───────────┐     forwarder      ┌──────── guest B ─────┐
│ import (B, "hello")        │ ─────────────────► │ export hello         │
└────────────────────────────┘                    └──────────────────────┘
```

### Host → guest

Python (or C host via the same API) calls the accessor; loader
`wasm_runtime_call_*` into the instance.

### Guest → host **(shipped: call slots + mem cookies + handles)**

Guest declares Wasm imports under `micropython.host`. The loader registers:

| import | sig | role |
|--------|-----|------|
| `call_i32` | `(i32,i32)->i32` | `wasm.host_set(slot, fn)` → `fn(arg)` |
| `call0_i32` | `(i32)->i32` | `fn()` with no arg |
| `call_i64` | `(i32,i64)->i64` | same, i64 |
| `call_f32` | `(i32,f32)->f32` | same, f32 |
| `call_f64` | `(i32,f64)->f64` | same, f64 |
| `call_buf` | `(slot,off,len)->i32` | `fn(bytes)` from linear `[off,len]` |
| `call_mem` | `(slot,cookie)->i32` | `fn(bytes)` from host **cookie** |
| `call_obj` | `(slot,handle)->i32` | `fn(obj)` from Python **handle** |
| `mem_alloc` / `mem_free` / `mem_len` | … | host-heap cookies (Metal-style) |
| `mem_copy_in` / `mem_copy_out` | `(cookie,off,n)->i32` | linear ↔ cookie |
| `mem_copy_*_at` | + cookie offset | partial copy |

**Pointers:** never pass raw host pointers. Guest `char*` is an `i32` linear
offset (`MP_WASM_PTR(p)` in `guest.h`). Durable buffers → **cookies**
(`mem_alloc` + `mem_copy_*`); opaque Python values → **handles**
(`wasm.handle_register` / `resolve` / `free`). From Python,
`WasmModule.memory_read/write/alloc/free` touch linear memory directly;
`wasm.mem_alloc/get/set/free` manage cookies.

Python installs callables with `wasm.host_set` / `host_get` / `host_clear`.
Slots **grow on demand**. Do **not** list host imports in pack.toml
`[[imports]]` (guest→guest only). Soft-fail returns `-1` / `-1.0` if the
slot is empty or the callable raises.

Py↔Wasm binders and guest→guest forwarders accept **i32 / i64 / f32 / f64**
(any arity; multi-result → Python tuple). Names use up to 255 chars (qstr).
v128 / externref are not bound (not useful for the Python surface).

### Guest → guest

Pack declares needed `(module, func)` pairs (see `micropython.imports`
below). At instantiate, the loader registers **forwarding natives** that
resolve the target pack by package name and call into its export. If the
target is missing/unloaded, call fails soft (sentinel / exception), not
UB.


## File anatomy

```text
┌──────────────────────────────────────────────┐
│ Wasm module (linked C + C++ + Rust)          │
│   imports: micropython.host.*, peer forwards │
│   exports: native API + optional load/unload │
├──────────────────────────────────────────────┤
│ custom "micropython.pack"                    │
│   name, files[], exports[]  (v2+ table)      │
├──────────────────────────────────────────────┤
│ custom "micropython.imports"  (optional)     │
│   [{module, func}, ...] guest→guest needs    │
└──────────────────────────────────────────────┘
```

## Custom section: `micropython.pack`

| Field | Encoding | Notes |
|-------|----------|--------|
| section id | `0` | Wasm custom section |
| name | UTF-8 | exactly `micropython.pack` |
| payload | below | |

### Payload version 1 **(shipped)**

Little-endian. Files only — enough to carry a Python tree.

```text
magic        4   b"MPWP"
version      2   1
flags        2   0
name_len     2
name         N   package root (e.g. "mypkg")
n_files      4
n_files ×:
  path_len   2
  path       P   relative, '/', no ".."
  kind       1   1=.py  2=.mpy  3=raw
  data_len   4
  data       D
```

| path in pack | `sys.modules` key |
|--------------|-------------------|
| `__init__.py` | `mypkg` |
| `util.py` | `mypkg.util` |
| `sub/mod.mpy` | `mypkg.sub.mod` |

### Payload version 2 **(proposed)**

Same header with `version = 2`, then after `n_files` entries:

```text
n_exports     4
n_exports ×:
  module_len  2
  module      M   relative dotted suffix ("" = root)
  func_len    2
  func        F   Python attribute name
  export_len  2
  export      E   Wasm export name
  sig         1   signature tag (see below)
```

**Signature tags (initial):**

| sig | meaning |
|-----|---------|
| `0` | `() -> i32` |
| `1` | `(i32) -> i32` |
| `2` | `(i32,i32) -> i32` |
| `3` | `(i32…) -> i32` up to implementation max |
| `255` | unbound / call only via low-level `WasmModule.call` |

Unknown `sig` → skip binding that export (still callable via low-level API).

### Kind / flags

| kind | meaning |
|------|---------|
| `1` | `.py` source |
| `2` | `.mpy` bytecode |
| `3` | raw resource |

v1 flags: none defined; ignore unknown bits. Readers that do not know
`version` skip the whole section.

## Custom section: `micropython.imports` **(proposed)**

Guest→guest import list:

```text
magic     4   b"MPWI"
version   2   1
n_imports 4
n_imports ×:
  module_len  2
  module      M   target package name (e.g. "greeter")
  func_len    2
  func        F   export / attribute name on that pack
```

Loader reads this **before** instantiate and registers one forwarder native
per pair (Wasm import module = target package name, field = `func`).

## Tooling shape

```text
tools/wasm_pack.py [pack.toml | pack dir | sources…]
  0. read pack.toml (if present / if given a directory)
  1. compile native sources (impl / globs / sources list) → objects
  2. wasm-ld → linked.wasm (exports from [[exports]] + lifecycle)
  3. optional: freeze [python].mount .py → .mpy via mpy-cross (--freeze / freeze=true)
  4. append micropython.pack  (name + tree [+ exports v2]; kind=1 .py / kind=2 .mpy)
  5. append micropython.imports from [[imports]]
```

CLI may stay source-oriented for smoke tests; directory + `pack.toml` is
the normal pack author path (see `examples/hello/pack.toml`):

```sh
python3 tools/wasm_pack.py examples/hello -o hello.wasm
```

## Loader API (proposed Python surface)

```python
import wasm

# Low-level (exists today): bytes/path → instance, .call()
m = wasm.load("hello.wasm")
m.call("add", 2, 3)

# Full pack load → sys.modules + accessors + py children
wasm.load_pack("/lib/hello.wasm")     # or https://…
import hello                          # natural
import hello.util                     # py child of native pack

# Wasm-aware import (search path, children, load-or-reuse)
sensor = wasm.import_wasm("sensor")
drivers = wasm.import_wasm("sensor.drivers")

wasm.unload("sensor")
```

Low-level escape remains: `WasmModule.call("export", ...)`.

## Port hooks (host; not in the pack bytes)

Keep optional and boring for upstream:

| Hook | Role |
|------|------|
| `MICROPY_WASM_MALLOC` / `FREE` | instance allocator |
| `MICROPY_WASM_FETCH(uri)` | VFS/HTTP (or custom) → bytes |
| `MICROPY_WASM_VERIFY(bytes, sig)` | signature check; default mbedtls when enabled |
| `MICROPY_WASM_EXPORT_PUBLISH(mod, func, ptr)` | optional observe/publish hook for ports |
| host native table | implements `micropython.host.*` imports |

## Phased delivery

| Phase | Deliver |
|-------|---------|
| **A — now** | v1 pack section (files + name); native-only `call`; tool compiles C |
| **B** | `load_pack` → `sys.modules` + embedded `.py` / `.mpy` tree; natural child imports |
| **C** | v2 export table + accessors / trampolines |
| **D** | `import_wasm` + path search (`foo.wasm` / `foo/__init__.wasm`) |
| **E** | `micropython.imports` + guest→guest forwarders |
| **E2** | `micropython.host.call*_i32` + `wasm.host_set` (guest→Py) |
| **F** | Mixed C/C++/Rust in `wasm_pack`; lifecycle exports |
| **G** | HTTP fetch hook + optional mbedtls signature verify |
| **H** | `wasm.install_hook()` (+ optional `builtinimport` path-walk) so `import` auto-loads packs |
| **I** | AOT: `MICROPY_PY_WASM_AOT`, finder prefers `.aot`, `wamrc` in pack tool docs |

## Compatibility

- `.wasm` without `micropython.pack` = native-only guest (supported).
- v1 readers ignore v2 trailing export tables if we bump version (v2 readers
  read both files + exports).
- Prefix `micropython.` on custom section names is reserved for this feature.
- No requirement that Python paths mirror `native/` paths — native is one
  blob; Python is the multi-level namespace.


## File layout

| Path | Role |
|------|------|
| `extmod/wasmmod/` | Loader (`wasmmod`, runtime, pack, forward, host, finder, fetch, verify) |
| `extmod/extmod.mk` / `.cmake` | Gate + link `libiwasm` |
| `py/mpconfig.h` | `MICROPY_PY_WASM` / `_AOT` / `MICROPY_WASM_VERIFY` (default 0) |
| `ports/unix/mpconfigport.mk` | Unix knobs (default off) |
| `.gitmodules` + `lib/wamr` | WAMR submodule |
| `tools/wasm_pack.py` / `wasm_sign.py` | Pack / sign tooling |
| `examples/wasmmod/` | Demos + `run_matrix.py` + this doc |
| `tests/extmod/wasm_*.py` | CI tests (planned) |
| `docs/library/wasm.rst` | User docs (planned) |

### PR hygiene

- Feature entirely behind `MICROPY_PY_WASM` (default off).
- Port hooks stay weak/`#ifndef` defaults (`MALLOC`, `FETCH`, `VERIFY`,
  `EXPORT_PUBLISH`).

## Author

Rouven Raudzus (`raudzus@pymergetic.com`) — [pymergetic](https://github.com/pymergetic)
