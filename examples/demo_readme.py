import sys
sys.implementation
sys.implementation.name
help("modules")

# Before the hook: hello/ on cwd is an empty directory package — no pack attrs.
import hello
hasattr(hello, "greet")

import wasm
wasm
wasm.install_hook()

# Drop the stale empty module, then import again — finder loads hello/hello.wasm.
del sys.modules["hello"]
import hello, mixed, bridge
hasattr(hello, "greet")

hello.greet()
hello.answer()
mixed.mixed_answer()
bridge.via_rs(6)
bridge.via_hello()
bridge.via_peer_hello()
