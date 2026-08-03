# pyright: reportMissingImports=false
import sys
sys.implementation
sys.implementation.name
help("modules")

# Before hook: hello/ on cwd is an empty directory package.
import hello
hasattr(hello, "greet")

import wasm
wasm
wasm.install_hook()

# After hook: drop stale module; finder loads hello/hello.wasm.
del sys.modules["hello"]
import hello, mixed, bridge
hasattr(hello, "greet")

hello.greet()
hello.answer()
mixed.mixed_answer()
bridge.via_rs(6)
bridge.via_hello()
bridge.via_peer_hello()
