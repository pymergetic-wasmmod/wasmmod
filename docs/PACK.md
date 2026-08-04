# MicroPython WASM pack format

A **pack** is one `.wasm` deliverable: mixed native code (C / C++ / Rust)
linked into a single Wasm module, plus an optional multi-level Python tree
(`.py` / `.mpy`) embedded in a custom section. At load time the pack is
registered into normal MicroPython `sys.modules` so `import pkg.sub` works
like any other package.

Today’s loader (`extmod/wasmmod/wasmmod.c`) only instantiates Wasm and offers
`call()`. This document is the **target design** for packing, registration,
and host↔guest / guest↔guest calls. Bits marked **v1 (shipped)** exist via
`tools/wasmmod.py pack` now; the rest is proposed structure.

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

Everything lives under **`src/`**. Python paths mirror the module tree;
C/C++/Rust may sit **beside** the package level they belong to. The pack
name comes from `pack.toml`; the loader never sees a `src` segment —
`wasm_pack` strips the mount prefix when embedding.

Native sources still compile and link into **one** Wasm module; only the
**export table** (not directory layout) names what shows up as native
attrs in Python. Nested natives are organizational unless you set
`[[exports]] module=…`.

```text
mypkg/                          # pack root
  pack.toml                     # manifest (TOML)
  src/                          # default native.dir + python.mount
    __init__.py                 # → mypkg
    foo.c                       # co-located native at pack root level
    bar.rs
    util/
      __init__.py               # → mypkg.util
      helper.c                  # sits beside that submodule (optional)
      extra.py
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
src/**/*.{c,cc,rs}  ── compile + wasm-ld ──┐
                                           ├──► mypkg.wasm
src/**/*.{py,…}     ── embed as section ───┘  (paths relative to src/)
```

Analogy: think of the pack as a MicroPython **extension module** (CPython’s
`.pyd` / `.so` idea), except the native part is Wasm and the pure-Python
parts travel inside the same file instead of beside it on a filesystem.

## Manifest: `pack.toml`

Human-facing description of the pack. The tool reads it; selected
fields are baked into `wasmmod.pack` / `wasmmod.imports`. TOML for
lists/comments.

### Shape (proposed)

```toml
# Required
type = "package"              # wasm pack kind
name = "mypkg"                # import root / sys.modules key
version = "0.1.0"

# Optional blurb (tooling/docs; also used by `wasmmod publish` as description)
comment = "Mixed native + Python demo pack"
description = "Longer summary for the CDN package page"
license = "MIT"
homepage = "https://example.com/mypkg"
author = "dev@example.com"   # → CDN maintainer_email / author page

# Languages present under src/ (mixed allowed).
# Tools use this to pick compilers; empty = discover by file extension.
# "cpp" / "rs" are impl languages only — exported surface is still C ABI.
impl = ["c", "rs"]            # subset of: "c" | "cpp" | "rs"

# CDN / install closure (→ wasmmod.deps). Exact versions.
# Distinct from [[imports]] (guest→guest call graph; no versions).
[deps]
greeter = "0.1.0"

[python]
# mount = "src"               # default; tree → wasmmod.pack (strip prefix)
keep_source = true            # keep .py beside bytecode (recommended)
# freeze = true               # opt-in bytecode (default source-only)
# targets = [                 # host-tagged names in the pack section
#   "upy:mpy6:sib31",
#   "upy:mpy6:sib63",
#   "cpy:cp312",
# ]

[native]
# dir = "src"                 # default; recurse for .c/.cpp/.rs
# sources = ["src/foo.c"]     # optional explicit list (else discover dir)

[lifecycle]
load = "mp_pack_load"         # wasm export name, or "" to omit
unload = "mp_pack_unload"

# Published into sys.modules as accessors (→ wasmmod.pack exports[] v2)
# Loader introspects Wasm types; `sig` is optional/legacy and can be omitted.
[[exports]]
func = "add"                  # Python attr; Wasm export defaults to same name

[[exports]]
module = "sub"                # optional: nest under mypkg.sub
func = "ping"
export = "sub_ping"           # optional: when Wasm export ≠ func

# Guest→guest needs (→ wasmmod.imports)
[[imports]]
module = "greeter"            # other pack's `name`
func = "hello"
```

`[deps]` vs `[[imports]]`:

| Field | Section | Purpose |
|-------|---------|---------|
| `[deps]` | `wasmmod.deps` (MPWD) | Install/CDN closure: package **name → exact version** |
| `[[imports]]` | `wasmmod.imports` (MPWI) | Runtime forwarders: **(module, func)** call graph |

Nested child packs still need their parent via normal Python imports; `[deps]` is for **peer packages** the CDN resolver / device install order must fetch.

### Minimal manifest

```toml
type = "package"
name = "hello"
version = "0.1.0"
impl = ["c"]

# [python] / [native] default to src/ (optional overrides)

[[exports]]
func = "hello"

[[exports]]
func = "add"
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
2. Instantiate Wasm (resolve `wasmmod.imports` / host imports).
3. Call optional `mp_pack_load` (lifecycle).
4. Create / reuse the root module object; store in **`sys.modules["mypkg"]`**.
5. Publish embedded Python tree under that root (virtual filesystem +
   import hook, or eager `exec` into submodule objects):
   - `src/util.py` → `sys.modules["mypkg.util"]`
   - `src/sub/__init__.py` → package `mypkg.sub`
   - `src/sub/mod.py` → `sys.modules["mypkg.sub.mod"]`
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
| `mypkg.wasm` / `mypkg.aot` / `mypkg.elf` | **Module pack** (interp / AOT / in-tree ELF; name from manifest or stem) | `mypkg` |
| `mypkg/__init__.wasm` / `.aot` / `.elf` | **Package pack** (directory is the package) | `mypkg` |
| `mypkg/sub.wasm` / `.aot` / `.elf` | Child pack under package `mypkg` | `mypkg.sub` (nested pack) |
| `mypkg/sub/__init__.wasm` / `.aot` / `.elf` | Nested package pack | `mypkg.sub` |
| `mypkg.sub.wasm` | Flat dotted filename (e.g. under `packs/`) | `mypkg.sub` |
| `mypkg.sub.aot{N}` | Versioned **AOT** format (`N` = WAMR `AOT_CURRENT_VERSION`, e.g. `.aot6`) | `mypkg.sub` |
| `mypkg.sub.<arch>.aot{N}` | Arch-tagged AOT (`wasm.arch` / `MICROPY_WASM_PACK_ARCH`) | `mypkg.sub` |
| `mypkg.sub.aot` | Legacy unversioned AOT (still probed after `.aot{N}`) | `mypkg.sub` |
| `mypkg.sub.elf` / `mypkg.sub.<arch>.elf` | **ELF64 ET_REL** pack (in-tree reloc loader; not `dlopen`) | `mypkg.sub` |

Probe order is `MICROPY_WASM_CONTAINERS` (with `MICROPY_PY_WASM_ELF=1`: default `elf,aot,wasm`; otherwise `wasm`). Browser builds use `wasm` only.

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

for root in wasm.path + sys.path:
  # path form / flat dotted (.wasm portable):
  try root/a/b/c/__init__.wasm | root/a/b/c.wasm | root/a.b.c.wasm
  # AOT — format-tagged .aot{N} (N = wasm.AOT_VERSION), then legacy .aot:
  try root/a/b/c[.<arch>].aot{N} | … .aot{N} | … .aot
  # each candidate also tries trailing .zlib first
```

So yes: **path-based finder**, same mental model as `sys.path`, just also
understands nested packs, flat dotted names, optional arch tags, and HTTP roots.

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
wasm.verify(False)                       # session gate: all loads (VFS + HTTP)
wasm.install_hook("https://example.org/packs/")  # optional URL root + import hook
# Multiple roots, first wins (PyPI-style index priority):
# wasm.install_hook(url=["https://cdn.example/packs/", "https://mirror.example/packs/"])
# wasm.path.append("/lib/wasm"); wasm.install_hook()

import sensor           # finder runs; loads sensor/__init__.wasm if needed
import sensor.drivers
from sensor import util # py child inside the pack — already natural

wasm.uninstall_hook()   # restore previous __import__
```

For **metal-cdn** (index + pin artifacts), prefer:

```python
wasm.cdn("http://127.0.0.1:8000/cdn", token)  # selects MetalCdnDriver
```

`install_hook("…/cdn")` also selects that driver when the URL looks like a CDN
base; flat pack mirrors keep `PathDriver`. See [Circular deps and phased load](#circular-deps-and-phased-load).

Under `MetalCdnDriver`, `metal_fetch` probes lead/pin URLs in the same
container order as the VFS finder (`MICROPY_WASM_CONTAINERS`), preferring
`.zlib` twins. For **ELF** and **AOT** it tries `name.<arch>.ext` from
`wasm.arch` before naked `name.ext`, then legacy `.aot` after `.aot{N}`:

```text
…/artifacts/lead/hello.x86_64.elf.zlib
…/artifacts/lead/hello.x86_64.elf
…/artifacts/lead/hello.elf.zlib
…/artifacts/lead/hello.elf
…/artifacts/lead/hello.x86_64.aot6.zlib   # when MICROPY_PY_WASM_AOT
…
…/artifacts/lead/hello.wasm.zlib
…/artifacts/lead/hello.wasm
```

Pin fetches use `/artifacts/pin/{ver}/…` with the same suffix list.

`wasm.verify()` returns the current flag; `wasm.VERIFY` is the compile-time
`MICROPY_WASM_VERIFY` mode (0/1/2). Runtime `verify(False)` skips checks even
when compile-time mode is on (unsigned smoke / local trees).

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

## ELF (in-tree ET_REL — no WAMR, no `dlopen`)

ELF packs are interchangeable containers beside `.wasm` / `.aot`:

- Filename: `name.elf` or `name.<arch>.elf` (+ optional `.zlib`)
- Kind: **ELF64 ET_REL** under the hood; wasmmod maps, relocates, and calls
  on the **host arch only** (`EM_X86_64` or `EM_AARCH64` matching the process)
- Metadata: `SHT_PROGBITS` sections `.wasmmod.pack` / `.imports` / `.deps` / `.sig` / `.source`
- Register / connect / MPWI: **same** as Wasm — only the execute engine differs
- Undef symbols: resolved at load via MPWI `func` name → registry peer
  (`ELF→ELF` direct pointer; `ELF→Wasm` via i32 PLT stubs). Host modules
  `wasmmod.host` / `wasmmod` bind to System V wrappers. Pointer args replace
  Wasm linear offsets for `call_buf` / `call_py` / `mem_copy_*` and loader
  `version` / `call_i32` (cookies/`call_mem`/`call_obj` / slot calls unchanged).
  `micropython.*` reserved — ELF load fails with
  `micropython.* not supported: <module>.<func>` (not registered for Wasm either).
- Relocs: x86_64 GOT/PC subset; aarch64 `CALL26`/`JUMP26`, ADRP/LO12, GOT page
- Build: `gcc -c -ffreestanding -fPIC …` then `wasmmod.py pack-elf`
  (optional `make -C examples/hello_elf aarch64` for a CDN twin).
  Prefer `-fPIC` so local data uses PC-relative relocs; `-fno-pic` needs
  a low-32-bit image map (`MAP_32BIT` on Linux x86_64) for `R_X86_64_32`.
  `pack-elf` embeds the same `[python]` mount tree as Wasm packs
  (examples/hello_elf freezes `.mpy` like the Wasm hello pack).
- Publish: `wasmmod.py publish … --elf packs/hello.elf [--arch x86_64]`
  stages/sign/zlib beside the Wasm build (sources under `--out-dir`).
- Enable: `MICROPY_PY_WASM_ELF=1`; preference via `MICROPY_WASM_CONTAINERS`
- Browser: wasm-only (no ELF execute)

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

`wasmmod.pack` / `wasmmod.imports` / `wasmmod.source` use the **same named
payloads** on both engines:

| Deliverable | How sections are stored |
|-------------|-------------------------|
| `.wasm` | Wasm custom sections (`id=0`) |
| `.aot` | WAMR AOT custom sections (`type=100`, RAW) via `wamrc --emit-custom-sections=…` |
| `.elf` | ELF64 `SHT_PROGBITS` named `.wasmmod.*` (+ WPSE cookie for strip/sign) |

`tools/wasmmod.py pack … --aot` always passes

`--emit-custom-sections=wasmmod.pack,wasmmod.imports,wasmmod.source`

so a **single `.aot` is enough** for execute + pack load + `wasm.source*` /
`wasmmod-read` (no sibling `.wasm` required for metadata). The `.wasm` remains
the portable build input. Host WAMR needs `WAMR_BUILD_LOAD_CUSTOM_SECTION=1`
(set by the wasmmod port fragment).

**Sign after AOT** (do not copy `wasmmod.sig` through `wamrc` — the digest
covers the final artifact bytes):

```sh
wamrc --emit-custom-sections=wasmmod.pack,wasmmod.source \
  -o hello.aot hello.wasm
tools/wasmmod.py sign sign --key leaf.key.pem \
  --chain chain.der hello.aot   # same model as hello.wasm
wasmmod-read sig hello.aot
```

Optional still: keep a portable `.wasm` beside arch-tagged `.aot` for hosts
that are interp-only.

Tooling:

```sh
python3 tools/wasmmod.py pack examples/hello/ -o hello.wasm --aot
# optional: wasm_pack --write-mpack hello.mpack
```

Signature verify hashes the **loaded artifact without `wasmmod.sig`**.
Same rules for `.wasm`, `.aot`, and `.elf`. One file — no detached `.sig`/`.crt`.

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
| **HTTP(S)** | Default: native C client in `fetch.c`. Replace via `mp_wasm_io_ops_t` (`io.h` / [ports/PORT.md](../ports/PORT.md)) — Metal async-ready. |

URL roots mirror the VFS layout. Candidate order matches local finder
(AOT `.aot{N}` + arch → legacy `.aot` → portable `.wasm`; each with `.zlib`
preferred; tree then flat dotted names).
HTTP roots are probed with a real HEAD/GET (only **200** counts); no directory listing.

```python
import wasm
wasm.verify(True)                        # session gate (default on): VFS + HTTP
wasm.install_hook("https://example.org/packs/")
# or: wasm.path.append("https://example.org/packs/"); wasm.install_hook()
import hello   # GET …/packs/hello.wasm (signed blob; wasmmod.sig inside)
```

`wasm.verify(False)` disables signature checks for the session (compile-time
`MICROPY_WASM_VERIFY=0` is always a no-op). Smoke:
`make -C examples test-http` (unsigned; uses `wasm.verify(False)`).

Ports replace I/O with `mp_wasm_io_set` / `MICROPY_WASM_IO_OPS` (see [ports/PORT.md](../ports/PORT.md)).

## Signature verification (mbedtls)

### Trust vs sign

| Layer | Scope | Material |
|-------|--------|----------|
| **Trust** | Host / upy project / Metal image | Root CA DER(s) zlib-compressed into ROM at **image build** (`MICROPY_WASM_TRUST_CA`), or flash via `MICROPY_WASM_TRUST_BOOT` / runtime `add_trust`. |
| **Sign** | Each pack build (CI) | Leaf private key → embed `wasmmod.sig` (MPWS: sig + chain). |
| **Ship** | With the pack | Signed `.wasm` / `.aot` only — never the root, never the leaf key. |

One leaf may sign many packs; or each build gets its own leaf under the same CA.
The device only checks that the embedded chain reaches a trusted root, then ECDSA over the
bytes. Multiple roots: `MICROPY_WASM_TRUST_CA="root_a.der root_b.der"` (or
repeated `add_trust`). Baked roots inflate **lazily** on first verify /
`trust_count()` — not on `import wasm`. Omit bake and skip `add_trust` →
`trust_n==0` → signed loads fail under `VERIFY=1`. `wasm.trust_clear()`
disables auto-reload of baked roots for the session.

Examples smoke generates keys under `.keys/` (gitignored). A **real** project
keeps the root in the firmware build and only the leaf in pack CI:

```bash
# firmware / upy image (one or more public root DERs → zlib ROM)
make MICROPY_PY_WASM=1 MICROPY_WASM_VERIFY=1 \
  MICROPY_WASM_TRUST_CA="/path/to/project/trust/root.crt.der"

# pack CI
tools/wasmmod.py sign sign --key sign/leaf.key.pem \
    --chain sign/chain.der packs/hello.wasm
```

Smoke key layout (`gen-pki -o .keys`):

```text
.keys/
  trust/root.crt.der     # baked via MICROPY_WASM_TRUST_CA (or add_trust)
  sign/leaf.key.pem      # CI secret
  sign/chain.der         # embedded into wasmmod.sig (MPWS)
```

**PKI (preferred):** trust a **root CA** (compile-in or `wasm.add_trust`).
Packs ship an embedded `wasmmod.sig` (MPWS: ECDSA + leaf [+ intermediates]; root
not included). The loader verifies the X.509 chain to the trusted root, then
checks the ECDSA-P256/SHA-256 signature with the leaf key.

**Pinned pubkey (still supported):** `wasm.add_trust(leaf.pub.der)` and a
raw (non-MPWS) `wasmmod.sig` — no chain check.

When `MICROPY_SSL_MBEDTLS` is on and `MICROPY_WASM_VERIFY!=0`, packs are
verified *before* instantiate.

### Distribution shape

**Embedded only** (`.wasm` / `.aot` / `.elf`):

- `tools/wasmmod.py sign sign --key … --chain chain.der PATH`
  → `wasmmod.sig` (Wasm custom / AOT CUSTOM/RAW / ELF `.wasmmod.sig` + WPSE) with MPWS (sig + chain)
- Loader hashes the artifact *without* that section
- Sign **each** deliverable after it is final (after `wamrc` for AOT; after `pack-elf` for ELF)

Inspect:

```bash
tools/wasmmod.py sign info hello.aot
tools/wasmmod.py sign verify --trust .keys/trust/root.crt.der hello.aot
wasmmod-read sig hello.aot
wasmmod-read verify --trust .keys/trust/root.crt.der hello.aot
# MicroPython: wasm.sig_info(buf); wasm.verify_sig(buf)  # needs trust loaded
```

```bash
make -C examples test-signed         # PKI sign + VFS + HTTP verify
# or piecemeal:
make -C examples sign-packs          # .keys/{trust,sign} + packs/*.{sig,crt}
make -C examples test-verify         # VERIFY=1 + baked MICROPY_WASM_TRUST_CA
make -C examples test-http-verify    # same over HTTP
```

```python
# Prefer bake at build time; trust_count() triggers lazy inflate:
# wasm.add_trust(open(".keys/trust/root.crt.der", "rb").read())
print(wasm.trust_count())  # >= 1 when MICROPY_WASM_TRUST_CA was set
# wasm.trust_clear()  # empties store; baked roots stay disarmed this session
```

Host tooling:

```bash
tools/wasmmod.py sign gen-pki -o .keys              # trust/ + sign/
tools/wasmmod.py sign gen-pki -o .keys --sub-ca     # root → sub → leaf
tools/wasmmod.py sign sign --key .keys/sign/leaf.key.pem \
    --chain .keys/sign/chain.der packs/hello.wasm
```

| Mode | Behaviour |
|------|-----------|
| `MICROPY_WASM_VERIFY=0` | no verify (default matrix smoke) |
| `MICROPY_WASM_VERIFY=1` | require valid sig every load |
| `MICROPY_WASM_VERIFY=2` | verify if `wasmmod.sig` present; else allow |

Failed verify → do not instantiate. The root CA never comes from the pack
itself — only from the image (`MICROPY_WASM_TRUST_CA` / `TRUST_BOOT`) or
`add_trust`.

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

Guest declares Wasm imports under `wasmmod.host`. The loader registers:

| import | sig | role |
|--------|-----|------|
| `call_i32` | `(i32,i32)->i32` | `wasmmod.host_set(slot, fn)` → `fn(arg)` |
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

**ELF guests** use the same import names with **native pointers** instead of
linear offsets: `call_buf(slot, ptr, len)`, `call_py(mod,mod_len,attr,attr_len[,arg])`,
`mem_copy_*(cookie, ptr, n)`, loader `version(buf, maxlen)` / `call_i32(…, args*)`.

**Pointers:** never pass raw host pointers. Guest `char*` is an `i32` linear
offset (`MP_WASM_PTR(p)` in `guest.h`). Durable buffers → **cookies**
(`mem_alloc` + `mem_copy_*`); opaque Python values → **handles**
(`wasm.handle_register` / `resolve` / `free`). From Python,
`WasmModule.memory_read/write/alloc/free` touch linear memory directly;
`wasm.mem_alloc/get/set/free` manage cookies.

Python installs callables with `wasmmod.host_set` / `host_get` / `host_clear`.
Slots **grow on demand**. Do **not** list host imports in pack.toml
`[[imports]]` (guest→guest only). Soft-fail returns `-1` / `-1.0` if the
slot is empty or the callable raises.

Py↔Wasm binders and guest→guest forwarders accept **i32 / i64 / f32 / f64**
(any arity; multi-result → Python tuple). Names use up to 255 chars (qstr).
v128 / externref are not bound (not useful for the Python surface).

### Guest → guest

Pack declares needed `(module, func)` pairs (see `wasmmod.imports`
below). At instantiate, the loader registers **forwarding natives** that
resolve the target pack by package name and call into its export. If the
target is missing/unloaded, call fails soft (sentinel / exception), not
UB.


## File anatomy

```text
┌──────────────────────────────────────────────┐
│ Wasm module (linked C + C++ + Rust)          │
│   imports: wasmmod.host.*, peer forwards │
│   exports: native API + optional load/unload │
├──────────────────────────────────────────────┤
│ custom "wasmmod.pack"                    │
│   name, files[], exports[]  (v2+ table)      │
├──────────────────────────────────────────────┤
│ custom "wasmmod.imports"  (optional)     │
│   [{module, func}, ...] guest→guest needs    │
├──────────────────────────────────────────────┤
│ custom "wasmmod.deps"  (optional)        │
│   [{name, version}, ...] install/CDN deps    │
└──────────────────────────────────────────────┘
```

## Custom section: `wasmmod.deps`

CDN / device install dependencies (exact versions). Emitted from pack.toml
`[deps]` by `wasmmod pack`. Magic **`MPWD`**, little-endian:

```text
magic       4  b"MPWD"
version     2  1
n_deps      4
n_deps ×:
  name_len  2
  name      N   package name
  ver_len   2
  ver       V   exact version string
```

On `metal-cdn` publish, if `PublishRequest.deps` is empty, the server fills
`PackageEntry.deps` from this section (via `inspect_upload`).

### Readers (C / Rust / Python)

| Lang | API |
|------|-----|
| C | `mp_wasm_deps_find_section` / `mp_wasm_deps_parse` ([`pack.h`](../pack.h) / `pack.c`) |
| Rust | `deps_find_section` / `deps_parse` (`extmod_rs/wasmmod/pack.rs`, rewrite/shim later) |
| Python | `parse_deps_payload` (`tools/wasmmod_pack.py`); metal-cdn `contents.parse_deps_payload` |

### Circular deps and phased load

`[deps]` may be **cyclic** (A needs B and B needs A). Guest→guest calls still
use `[[imports]]` / MPWI forwarders. The loader does **not** instantiate one
pack at a time when MPWD is present; it runs a closure:

1. **Resolve** — walk MPWD; cycles become one SCC (Tarjan).
2. **Fetch** — `PathDriver` (wasm.path / flat URL root) or `MetalCdnDriver`
   (`…/cdn` → `/artifacts/pin/{ver}/…`).
3. **Register** — MPWI forwarders + Wasm instantiate for every node; then
   `registry_add` for all names.
4. **Connect** — every MPWI guest target must be registered.
5. **Run** — `mp_pack_load` + embed Python for each node (deps-first; within an
   SCC, all are registered before any run).

Configure CDN from MicroPython:

```python
import wasm
wasm.cdn("http://127.0.0.1:8000/cdn", token)   # metal-cdn driver + optional Bearer
wasm.install_hook()
# Dotted names (Python package style); metal-cdn UI tree splits on '.'.
# Source may be sibling flat dirs or a nested monorepo (`wasmmod pack-tree`).
#   test_a / test_a.test_b.test_c / test_a.test_d
#   test_a2 / test_a2.test_b2.test_c2 / test_a2.test_d2
from test_a.test_b import test_c   # MPWD pulls peer test_a.test_d
from test_a2.test_b2 import test_c2
# or flat pack mirror:
# wasm.install_hook("https://example.org/packs/")
```

See also `examples/tree/` (sibling + `nested/`) and
`make -C extmod/wasmmod/examples test-tree{,-cdn}`.

**C runtime** (device loader): `mp_wasm_deps_*` in `pack.c`, `cdn.c` /
`resolve.c`, phased load in `packload.c`, `mp_wasm_connect_imports` in
`forward.c`, Python export `wasm.cdn`.

Under `MetalCdnDriver`, an MPWI peer missing from the closure is an error
(deps are authoritative). Path driver may still soft-connect for local peers.

Do not cross-call peer packs from module top-level until the Run phase for
that SCC has finished (safe window: after `mp_pack_load` for all peers).

## Custom section: `wasmmod.pack`


| Field | Encoding | Notes |
|-------|----------|--------|
| section id | `0` | Wasm custom section |
| name | UTF-8 | exactly `wasmmod.pack` |
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

### Payload version 2 **(shipped)**

Same header with `version = 2`, then after `n_files` entries (old file layout):

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

### Payload version 3 **(current writer)**

Same as v2 (always includes `n_exports`, may be 0), but each file entry is:

```text
  path_len   2
  path       P
  kind       1   1=.py  2=.mpy  3=raw  4=.pyc
  flags      1   bit0 = zlib-compressed payload
  raw_len    4   uncompressed size
  data_len   4
  data       D
```

Defaults: pack compress **on** for `.py` / `.mpy` / `.pyc` (skip if not smaller);
raw resources stay uncompressed. Toggle with `--compress` / `[pack] compress`.
Native Wasm/AOT code is never compressed here — use the whole-artifact MPZL
envelope for bandwidth.

### Kind / flags

| kind | meaning |
|------|---------|
| `1` | `.py` source |
| `2` | `.mpy` bytecode |
| `3` | raw resource |
| `4` | `.pyc` (CPython; ignored by MicroPython loader) |

v1/v2 flags: none; v3 file `flags` bit0 = zlib. Readers that do not know
`version` skip the whole section.

## Whole-artifact envelope: `*.zlib` (MPZL)

Optional outer wrap for `.wasm` / `.aot` / `.elf` (CDN / VFS bandwidth):

```text
magic     4   b"MPZL"
raw_len   4   uncompressed artifact size
zlib      …   RFC 1950 of the naked .wasm/.aot/.elf
```

Tools: `tools/wasmmod.py zlib wrap|unwrap|info`. Sign the naked artifact first,
then wrap. Finder prefers `path.wasm.zlib` / `path.aot{N}.zlib` /
`path[.arch].elf.zlib` when present; loader unwraps **before** verify/load.

CDN-oriented layout (channels + format-tagged AOT / ELF):

```text
…/packs/                    # lead / latest channel
…/packs/@0.1.0/             # version pin
  hello.wasm.zlib
  hello.elf.zlib            # in-tree ET_REL (host arch)
  hello.x86_64.elf.zlib     # arch-tagged ELF twin
  hello.aarch64.elf.zlib    # cross twin (rejected on other arches)
  hello.x86_64.aot6.zlib    # arch + AOT file-format N (= wasm.AOT_VERSION)
  hello.aot6.zlib           # format only (no arch infix)
```

Distribution software (index schema, HTTP publish, browser shell / sessions):
[pymergetic/metal-cdn](https://github.com/pymergetic/metal-cdn).
Guest catalog consume is `wasm.catalog()`; guest `wasm.publish*` stays a stub until
host `mp_wasm_io_ops_t.request` is wired.

## Custom section: `wasmmod.imports` **(proposed)**

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
tools/wasmmod.py pack [pack.toml | pack dir | sources…]
  0. read pack.toml (if present / if given a directory)
  1. compile native sources (impl / globs / sources list) → objects
  2. wasm-ld → linked.wasm (exports from [[exports]] + lifecycle)
  3. optional: freeze [python].mount .py → .mpy via mpy-cross (--freeze / freeze=true)
  4. append wasmmod.pack  (name + tree [+ exports v2]; kind=1 .py / kind=2 .mpy)
  5. append wasmmod.imports from [[imports]]
  6. optional: append wasmmod.source (`--with-source` / `[source] embed`)
```

CLI may stay source-oriented for smoke tests; directory + `pack.toml` is
the normal pack author path (see `examples/hello/pack.toml`):

```sh
python3 tools/wasmmod.py pack examples/hello -o hello.wasm --with-source \
  --tag arch=x86_64 --tag git=$(git rev-parse --short HEAD)
python3 tools/wasmmod.py source meta hello.wasm
python3 tools/wasmmod.py source extract hello.wasm -o ./hello-src/
```

## Custom section: `wasmmod.source` **(v1)**

Audit/source tree embedded in the same `.wasm` (signed with the module).
Magic **`MPSR`** (not `MPWS` — that envelope is used by `wasmmod.sig`).

```text
magic          4   b"MPSR"
format_ver     2   1
flags          2   0 (reserved)
name_len       2
name           N   pack import name
version_len    2
version        V   pack.toml version / --pkg-version
n_tags         2
n_tags ×:
  key_len      2
  key          K
  val_len      2
  val          V
n_files        4
n_files ×:
  path_len     2
  path         P   pack-root relative, '/' separators, no ".."
  flags        1   bit0 = zlib-compressed payload
  raw_len      4   uncompressed size
  data_len     4   on-wire size
  data         D
```

Defaults: source compress **on** (per file, skip if not smaller); pack
bytecode compress **on** (v3, same policy). Toggle with `--compress-source` /
`[source] compress` and `--compress` / `[pack] compress`. Whole-artifact
MPZL wrap is separate (`tools/wasmmod.py zlib`).

Contents: pack tree minus build artifacts (`.wasm`, `.aot`, `.elf`, `build/`,
`*.o`, …). Includes `pack.toml`, `src/**`, co-located markdown/licenses.

Runtime / tools:

```python
v = wasm.source_from_file("hello.wasm")  # or wasm.source("hello") if loaded
v.meta()       # {name, version, tags, n_files}
v.files()      # all stored paths
v.modules()    # dotted modules under src/
v.module("util").files()
v.read("src/__init__.py")
```

```sh
tools/wasmmod.py source meta|list|read|extract …
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

Keep optional and boring for upstream. Full contract: [ports/PORT.md](../ports/PORT.md).

| Hook | Role |
|------|------|
| `mp_wasm_io_ops_t` / `mp_wasm_io_set` | Preferred — replace HTTP fetch/probe (+ yield); Metal async→sync here |
| `MICROPY_WASM_IO_OPS` | Compile-time default ops table |
| `MICROPY_WASM_MALLOC` / `FREE` | instance allocator |
| `MICROPY_WASM_FETCH(uri)` | Legacy bool shim (prefer I/O ops) |
| `MICROPY_WASM_VERIFY_HOOK(bytes, sig)` | signature check; default mbedtls when enabled |
| `MICROPY_WASM_EXPORT_PUBLISH(mod, func, ptr)` | optional observe/publish hook for ports |
| host native table | implements `wasmmod.host.*` imports |

## Phased delivery

| Phase | Deliver |
|-------|---------|
| **A — now** | v1 pack section (files + name); native-only `call`; tool compiles C |
| **B** | `load_pack` → `sys.modules` + embedded `.py` / `.mpy` tree; natural child imports |
| **C** | v2 export table + accessors / trampolines |
| **D** | `import_wasm` + path search (`foo.wasm` / `foo/__init__.wasm`) |
| **E** | `wasmmod.imports` + guest→guest forwarders |
| **E2** | `wasmmod.host.call*_i32` + `wasmmod.host_set` (guest→Py) |
| **F** | Mixed C/C++/Rust in `wasm_pack`; lifecycle exports |
| **G** | HTTP fetch hook + optional mbedtls signature verify |
| **H** | `wasm.install_hook()` (+ optional `builtinimport` path-walk) so `import` auto-loads packs |
| **I** | AOT: `MICROPY_PY_WASM_AOT`, finder prefers `.aot`, `wamrc` in pack tool docs |
| **J** | ELF: `.elf` / `.<arch>.elf` in-tree ET_REL loader, `MICROPY_WASM_CONTAINERS`, publish `--elf` |

## Compatibility

- `.wasm` without `wasmmod.pack` = native-only guest (supported).
- v1 readers ignore v2 trailing export tables if we bump version (v2 readers
  read both files + exports).
- Prefix `wasmmod.` on custom section names is reserved for this feature
  (`wasmmod.pack` / `wasmmod.imports` / `wasmmod.sig`).
- Python under `src/` defines the module tree; co-located natives still link
  into one Wasm blob (export `module=` optional for nesting attrs).


## File layout (as submodule at `extmod/wasmmod`)

| Path | Role |
|------|------|
| `extmod/wasmmod/` | Loader + pack/runtime/forward/host/finder/fetch/verify |
| `extmod/wasmmod/third_party/wamr` | Nested WAMR submodule |
| `extmod/wasmmod/ports/micropython/` | Make/CMake glue; optional `mpconfig_wasm.h` |
| `extmod/wasmmod/tools/` | `wasmmod.py` + `wasmmod_*.py` |
| `extmod/wasmmod/examples/` | Demos + `run_matrix.py` |
| Host `extmod/extmod.mk` / `.cmake` | Thin `include` when `MICROPY_PY_WASM` |
| Host `examples/wasmmod` | Optional symlink → `extmod/wasmmod/examples` |

Host **does not** need `py/mpconfig.h` or `ports/unix/mpconfigport.mk` edits;
enable with `make MICROPY_PY_WASM=1`.

### PR hygiene

- Feature entirely behind `MICROPY_PY_WASM` (default off).
- Port hooks stay weak/`#ifndef` / ops-table defaults (`IO_OPS`, `MALLOC`,
  `VERIFY_HOOK`, `EXPORT_PUBLISH`) — see `ports/PORT.md`.
- Python bytecode selection stays in the **port** loader (upy vs cpy); shared
  code only parses the pack and runs WAMR.

## Author

Rouven Raudzus (`raudzus@pymergetic.com`) — [pymergetic](https://github.com/pymergetic)
