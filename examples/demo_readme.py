# pyright: reportMissingImports=false
import sys
sys.implementation
sys.implementation.name
help("modules")

import wasm
wasm
wasm.path.append("packs")
wasm.install_hook()
import hello, mixed, bridge

hello.greet()
hello.answer()
mixed.mixed_answer()
bridge.via_rs(6)
bridge.via_hello()
bridge.via_peer_hello()
