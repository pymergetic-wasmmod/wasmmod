# wasmmod-read

Standalone host reader for **`wasmmod.source`** (`MPSR`) and **`wasmmod.sig`** (`MPWS`)
on **`.wasm` or `.aot`**. Builds from a solo wasmmod checkout.

## Build

```sh
cargo build --release -p wasmmod-read
# → target/release/wasmmod-read
```

## Usage

Preferred via the unified dispatcher:

```sh
python3 tools/wasmmod.py read meta PATH.wasm
python3 tools/wasmmod.py read list PATH.aot
python3 tools/wasmmod.py read sig PATH.aot          # embedded signature meta
python3 tools/wasmmod.py read read PATH RELPATH
python3 tools/wasmmod.py read extract PATH -o DIR
python3 tools/wasmmod.py read verify --trust ROOT.crt.der PATH
```

Direct binary (same args after the subcommand):

```sh
./target/release/wasmmod-read meta PATH.wasm
```

Sign after pack/AOT (embed-only; no detached sidecars):

```sh
python3 tools/wasmmod.py pack examples/hello -o hello.wasm --with-source --aot
python3 tools/wasmmod.py sign sign \
  --key examples/.keys/sign/leaf.key.pem \
  --chain examples/.keys/sign/chain.der \
  hello.aot
python3 tools/wasmmod.py sign verify \
  --trust examples/.keys/trust/root.crt.der hello.aot
python3 tools/wasmmod.py read verify \
  --trust examples/.keys/trust/root.crt.der hello.aot
```

## Library

```rust
use wasmmod_read::{SigView, SourceView};

let src = SourceView::open_file("hello.aot")?;
let sig = SigView::open_file("hello.aot")?;
println!("mpws={} sig_len={}", sig.is_mpws, sig.sig.len());
```
