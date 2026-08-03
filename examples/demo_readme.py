import sys
sys.implementation
sys.implementation.name
help("modules")

import wasm
wasm
wasm.install_hook()
wasm.load_pack("hello/hello.wasm", "hello")
wasm.load_pack("mixed/mixed.wasm", "mixed")
wasm.load_pack("bridge/bridge.wasm", "bridge")
import hello, mixed, bridge

hello.greet()
hello.answer()
mixed.mixed_answer()
bridge.via_rs(6)
bridge.via_hello()
bridge.via_peer_hello()
