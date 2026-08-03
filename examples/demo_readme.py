# pyright: reportMissingImports=false
import sys
sys.implementation
sys.implementation.name
help("modules")

# Before hook: import "succeeds" as an empty package (hello/ dir on cwd,
# no __init__.py) — not a failure. Cached in sys.modules with no pack attrs.
import hello
hasattr(hello, "greet")

import wasm
wasm
wasm.install_hook()

# Re-import would reuse that cached empty module. Pop it so __import__ runs
# again; the hook then finds hello/hello.wasm and loads the real pack.
del sys.modules["hello"]
import hello, mixed, bridge
hasattr(hello, "greet")

hello.greet()
hello.answer()
mixed.mixed_answer()
bridge.via_rs(6)
bridge.via_hello()
bridge.via_peer_hello()
