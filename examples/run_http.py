# HTTP pack fetch smoke (unsigned). Driven by `make test-http`.
# Expects HTTP_PACK_ROOT env, e.g. http://127.0.0.1:8765/
# pyright: reportMissingImports=false

import os
import wasm

root = os.getenv("HTTP_PACK_ROOT")
if not root:
    raise SystemExit("HTTP_PACK_ROOT not set")

wasm.verify(False)  # unsigned smoke; applies to VFS + HTTP loads
wasm.install_hook(root)
import hello

assert hello.greet() == "hello from pack py"
assert hello.hello() == 42
assert hello.add(2, 3) == 5
print("test-http OK", root)
