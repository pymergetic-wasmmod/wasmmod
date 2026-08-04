# WASM guest modules

> **Experimental** (`wasmmod` **v0.1.4-alpha**). These examples and the host
> `wasm` module are pre-release: flags, pack contracts, and APIs may change.
> See the [package README](../README.md) status note.

Build freestanding guests and load them with MicroPython's optional `wasm`
module (`MICROPY_PY_WASM=1`). Wasm/AOT use WAMR; **ELF** packs (`.elf`) use the
in-tree ET_REL loader (`MICROPY_PY_WASM_ELF=1`, unix default).

**Pack format / design:** [docs/PACK.md](../docs/PACK.md) (`examples/PACK.md` is a
symlink) — mixed C/C++/Rust + Python tree, `sys.modules` registration,
load/unload, host↔guest / guest↔guest.

Commands below are run from the **host** tree root (MicroPython / metalpython)
with this repo checked out as `extmod/wasmmod` (metalpython may also symlink
`examples/wasmmod` → here).

## Cold worktree (once)

```sh
make -C ports/unix submodules
make -C mpy-cross BUILD=build -j"$(nproc)"
# nested WAMR (if not already):
git submodule update --init --recursive extmod/wasmmod
```

`mpy-cross` is required when packs enable `[python] freeze` / `targets` (`.mpy`).
Use a dedicated `BUILD=build` for mpy-cross so a host `BUILD=build-wasm` does
not leak into the mpy-cross output path.

## Quick start (full call matrix)

```sh
# Packs + unix host (build-wasm) + explained smoke (interpreter)
make -C extmod/wasmmod/examples test
# equivalent if symlink exists:
# make -C examples/wasmmod test

# ELF container smoke (hello / client peer / hostcall → slots+buf+mem+py+version)
make -C extmod/wasmmod/examples test-elf

# Interp + AOT + Fast JIT (+ LLVM JIT if a complete LLVM is available)
make -C extmod/wasmmod/examples test-engines
# make -C examples/wasmmod test-engines
```

That runs the call matrix and prints **lang×lang tables** (host→guest,
guest→host, guest→guest, same-pack) with index numbers, then a catalog of
each call. See [run_matrix.py](run_matrix.py).

### Dotted module tree (CDN + deps)

Two source layouts, same runtime/CDN tree (names split on `.`):

**Sibling / flat** (different-repo style) under `tree/`:

```text
tree/test_a/                  → packs/test_a.wasm
tree/test_a_test_d/           → packs/test_a.test_d.wasm
tree/test_a_test_b_test_c/    → packs/test_a.test_b.test_c.wasm
```

**Nested monorepo** (`type = "package"` markers; `wasmmod pack-tree` walks them):

```text
tree/nested/test_a2/                 → test_a2.wasm
  test_b2/                           (namespace only — no pack.toml)
    test_c2/                         → test_a2.test_b2.test_c2.wasm
  test_d2/                           → test_a2.test_d2.wasm
```

```sh
make -C extmod/wasmmod/examples test-tree       # offline both suites
make -C extmod/wasmmod/examples test-tree-cdn   # publish + wasm.cdn
# or: python3 tools/wasmmod.py pack-tree examples/tree/nested/test_a2 -o examples/packs
```

Smoke (`run_tree_cdn.py`):

```python
import wasm
wasm.path.append("packs")  # or wasm.cdn("http://127.0.0.1:8000/cdn")
wasm.install_hook()
import test_a
from test_a.test_b import test_c
assert test_c.c_answer() == 42
import test_a2
from test_a2.test_b2 import test_c2
assert test_c2.c2_answer() == 42
```

| Target | Host `BUILD=` | Needs |
|--------|---------------|--------|
| `test` | `build-wasm` | — |
| `test-elf` | `build-wasm` | `hello.elf` / `client.elf` / `hostcall.elf` + `hello.aarch64.elf` (arch reject); `run_elf.py` |
| `test-tree` | `build-wasm` | tree packs (offline path) |
| `test-tree-cdn` | `build-wasm` | running metal-cdn (dotted names) |
| `test-aot` | `build-wasm-aot` | matching `wamrc` from nested `third_party/wamr` |
| `test-fast-jit` | `build-wasm-fjit` | `-lstdc++` |
| `test-jit` | `build-wasm-jit` | full LLVM cmake (soft-skips if missing) |
| `test-engines` | all of the above | — |

Test hosts set `MICROPY_PY_WASM_MATRIX=1` so matrix-only helpers
(`host_matrix.rs`, `wasm.c_call` / `host_*_triple`) are linked.
Default `MICROPY_PY_WASM` builds omit them.

AOT uses `ports/unix/build-wamrc/wamrc` (built from
`extmod/wasmmod/third_party/wamr`). Override with `WAMRC=…` if needed.
If cmake still points at a stale WAMR path: `rm -rf ports/unix/build-wamrc`.

## Pack manifests (`pack.toml`)

Minimal export surface — loader introspects Wasm types; no `sig` required:

```toml
[[exports]]
func = "hello"

[[exports]]
func = "add"
```

Optional: `export = "…"` when the Wasm symbol ≠ Python name; `module = "sub"`
to nest under `pack.sub`.

Python payload (default **source-only**):

```toml
[python]
# mount = "src"   # default (with native.dir); strip prefix in pack paths
# freeze = true
# keep_source = true
# targets = [
#   "upy:mpy6:sib31",
#   "upy:mpy6:sib63",
#   "cpy:cp312",
# ]
```

With `freeze` / `targets`, `wasm_pack` embeds host-tagged bytecode
(`util.upy.mpy6.sib31.mpy`, `util.cpy.cp312.pyc`, …). The **MicroPython**
loader picks the best compatible `.mpy` (else `.py`) and ignores `.pyc`.
A future CPython port would do the inverse. CLI:
`--freeze` / `--no-freeze`, `--python-target …`, `--keep-source`.

## ELF packs (no WAMR)

Same register/connect/sign path as Wasm; execute is in-tree ET_REL:

| Example | Artifact | Notes |
|---------|----------|--------|
| `hello_elf/` | `packs/hello.elf` | freestanding exports + shared `hello` Python tree (`-fPIC`) |
| `client_elf/` | `packs/client.elf` | peer `hello` via MPWI + GOT |
| `host_elf/` | `packs/hostcall.elf` | `wasmmod.host` slots + `via_buf` / `via_mem` / `via_py` + loader `version` |
| `ticks/` | `packs/ticks.wasm` | positive: `micropython.runtime.ticks_ms` (Wasm) |
| `ticks_elf/` | `packs/ticks.elf` | positive: `micropython.runtime.ticks_ms` (ELF) |
| `bad_upy_elf/` | `/tmp/wasmmod_badupy.elf` | negative: unknown `micropython.*` → load error |

```sh
make -C extmod/wasmmod/examples/hello_elf
make -C extmod/wasmmod/examples/hello_elf aarch64   # CDN twin (not runnable on x86)
make -C extmod/wasmmod/examples test-elf
# Guest C: -ffreestanding -fPIC -fno-plt (prefer -fPIC; -fno-pic needs MAP_32BIT)
# pack-elf CLI: python3 tools/wasmmod.py pack-elf --obj foo.o --manifest pack.toml -o foo.elf
```

## Manual pieces

```sh
# 1) Build guest packs
make -C extmod/wasmmod/examples/hello
make -C extmod/wasmmod/examples/hello_elf
make -C extmod/wasmmod/examples/client   # guest→guest → hello
make -C extmod/wasmmod/examples/client_elf
make -C extmod/wasmmod/examples/host_elf
make -C extmod/wasmmod/examples/mixed    # C + Rust in one pack
make -C extmod/wasmmod/examples/bridge   # full matrix bridge

# 2) Build unix MicroPython with the wasm loader
make -C ports/unix MICROPY_PY_WASM=1 MICROPY_PY_BTREE=0 MICROPY_PY_FFI=0 BUILD=build-wasm

# 3) Hello smoke (native + embedded src/, including nested hello.util)
ports/unix/build-wasm/micropython -c '
import wasm
m = wasm.load_pack("extmod/wasmmod/examples/packs/hello.wasm")
print(m.hello(), m.add(2, 3), m.greet())
import hello.util
import hello.util.extra
print(hello.util.ping(), hello.util.extra.twice(21))
wasm.unload("hello")
'
```

### Guest → host (Python callbacks)

```python
import wasm
wasmmod.host_set(0, lambda x: x * 2)   # slot for wasmmod.host.call_i32
wasm.load_pack("extmod/wasmmod/examples/packs/hello.wasm")
wasm.load_pack("extmod/wasmmod/examples/packs/mixed.wasm")
b = wasm.load_pack("extmod/wasmmod/examples/packs/bridge.wasm")
print(b.via_host(7))   # → 14
wasmmod.host_clear()
```

Requires `clang` and a `wasm-ld` (LLVM lld, wasi-sdk, or rustup's wasm-ld).
Set `WASM_CC` / `WASM_LD` if needed. Mixed/bridge also need a Rust
`wasm32-unknown-unknown` target (`rustup target add wasm32-unknown-unknown`).

```sh
# Pack from manifest (tool lives in this repo)
python3 extmod/wasmmod/tools/wasmmod.py pack extmod/wasmmod/examples/hello -o hello.wasm
```

## Author

Rouven Raudzus (`raudzus@pymergetic.com`) — [pymergetic](https://github.com/pymergetic)
