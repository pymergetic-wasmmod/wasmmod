# pyright: reportMissingImports=false
import sys
sys.implementation
sys.implementation.name
help("modules")

import pymergetic.wasmmod as wasm
wasm
wasm.path.append("packs")
wasm.install_hook()
import pymergetic.wasmmod_examples.hello as hello
import pymergetic.wasmmod_examples.mixed as mixed
import pymergetic.wasmmod_examples.bridge as bridge

hello.greet()
hello.answer()
mixed.mixed_answer()
bridge.via_rs(6)
bridge.via_hello()
bridge.via_peer_hello()
