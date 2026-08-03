# WASM guest modules

Build freestanding guests and load them with MicroPython's optional `wasm`
module (WAMR-backed, `MICROPY_PY_WASM=1`).

**Pack format / design:** [PACK.md](PACK.md) (same as [docs/PACK.md](../docs/PACK.md)) —
mixed C/C++/Rust + Python tree, `sys.modules` registration, load/unload,
host↔guest / guest↔guest.

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

# Interp + AOT + Fast JIT (+ LLVM JIT if a complete LLVM is available)
make -C extmod/wasmmod/examples test-engines
# make -C examples/wasmmod test-engines
```

That runs the call matrix and prints **lang×lang tables** (host→guest,
guest→host, guest→guest, same-pack) with index numbers, then a catalog of
each call. See [run_matrix.py](run_matrix.py).

| Target | Host `BUILD=` | Needs |
|--------|---------------|--------|
| `test` | `build-wasm` | — |
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

## Manual pieces

```sh
# 1) Build guest packs
make -C extmod/wasmmod/examples/hello
make -C extmod/wasmmod/examples/client   # guest→guest → hello
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
python3 extmod/wasmmod/tools/wasm_pack.py extmod/wasmmod/examples/hello -o hello.wasm
```

## Author

Rouven Raudzus (`raudzus@pymergetic.com`) — [pymergetic](https://github.com/pymergetic)
