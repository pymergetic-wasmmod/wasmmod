# wasmmod-read

Standalone host reader for **`wasmmod.source`** (`MPSR`), **`wasmmod.sig`** (`MPWS`),
and **container sections** on **`.wasm` / `.aot` / `.elf`**. Builds from a solo wasmmod checkout.

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
python3 tools/wasmmod.py read sections PATH         # list container sections
python3 tools/wasmmod.py read section PATH INDEX [--hex]
```

Direct binary (same args after the subcommand):

```sh
./target/release/wasmmod-read meta PATH.wasm
./target/release/wasmmod-read sections PATH.elf
./target/release/wasmmod-read section PATH.wasm 3 --hex
./target/release/wasmmod-read symbols PATH.elf
./target/release/wasmmod-read addr2line PATH.elf 0x10
./target/release/wasmmod-read locations PATH.elf hello
./target/release/wasmmod-read disasm PATH.elf INDEX [OFFSET [LIMIT]]
./target/release/wasmmod-read has-dwarf PATH.elf
./target/release/wasmmod-read mpy PATH.mpy
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
use wasmmod_read::{list_sections, section_payload, SigView, SourceView};

let src = SourceView::open_file("hello.aot")?;
let sig = SigView::open_file("hello.aot")?;
let secs = list_sections(&std::fs::read("hello.wasm")?)?;
let code = section_payload(&std::fs::read("hello.wasm")?, secs.iter().find(|s| s.name == "code").unwrap().index)?;
println!("mpws={} sig_len={} code={}", sig.is_mpws, sig.sig.len(), code.len());
```
