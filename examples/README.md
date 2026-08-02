# WASM guest modules

Build freestanding guests and load them with MicroPython's optional `wasm`
module (WAMR-backed, `MICROPY_PY_WASM=1`).

**Pack format / design:** [PACK.md](PACK.md) — mixed C/C++/Rust + Python tree,
`sys.modules` registration, load/unload, host↔guest / guest↔guest.

## Quick start (full call matrix)

```sh
# Build all demo packs, unix host with wasm, run explained smoke (interpreter)
make -C examples/wasmmod test

# Also: AOT + Fast JIT (+ LLVM JIT if a complete LLVM is available)
make -C examples/wasmmod test-engines
```

That runs the full call matrix and prints **lang×lang tables** (host→guest,
guest→host, guest→guest, same-pack) with index numbers, then a catalog of
each call. See [run_matrix.py](run_matrix.py).

| Target | Host `BUILD=` | Needs |
|--------|---------------|--------|
| `test` | `build-wasm` | — |
| `test-aot` | `build-wasm-aot` | matching `wamrc` (built from `lib/wamr`) |
| `test-fast-jit` | `build-wasm-fjit` | — (`-lstdc++`) |
| `test-jit` | `build-wasm-jit` | full LLVM cmake (soft-skips if missing) |

Test hosts set `MICROPY_PY_WASM_MATRIX=1` so matrix-only helpers
(`examples/wasmmod/host_matrix.rs`, `wasm.c_call` / `host_*_triple`) are linked.
Default `MICROPY_PY_WASM` builds omit them.

## Manual pieces

```sh
# 1) Build guest packs
make -C examples/wasmmod/hello
make -C examples/wasmmod/client   # guest→guest → hello
make -C examples/wasmmod/mixed    # C + Rust in one pack
make -C examples/wasmmod/bridge   # full matrix bridge

# 2) Build unix MicroPython with the wasm loader
make -C ports/unix MICROPY_PY_WASM=1 MICROPY_PY_BTREE=0 MICROPY_PY_FFI=0

# 3) Hello smoke (native + embedded py/, including nested hello.util)
ports/unix/build-standard/micropython -c '
import wasm
m = wasm.load_pack("examples/wasmmod/hello/hello.wasm")
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
wasm.host_set(0, lambda x: x * 2)   # slot for micropython.host.call_i32
wasm.load_pack("examples/wasmmod/hello/hello.wasm")
wasm.load_pack("examples/wasmmod/mixed/mixed.wasm")
b = wasm.load_pack("examples/wasmmod/bridge/bridge.wasm")
print(b.via_host(7))   # → 14
wasm.host_clear()
```

Requires `clang` and a `wasm-ld` (LLVM lld, wasi-sdk, or rustup's wasm-ld).
Set `WASM_CC` / `WASM_LD` if needed. Mixed/bridge also need a Rust
`wasm32-unknown-unknown` target (`rustup target add wasm32-unknown-unknown`).

```sh
# Pack from manifest
python3 tools/wasm_pack.py examples/wasmmod/hello -o hello.wasm
```

## Author

Rouven Raudzus (`raudzus@pymergetic.com`) — [pymergetic](https://github.com/pymergetic)
