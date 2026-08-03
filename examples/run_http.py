# HTTP pack fetch smoke (unsigned). Driven by `make test-http`.
# Expects HTTP_PACK_ROOT env, e.g. http://127.0.0.1:8765/
# pyright: reportMissingImports=false

import os
import sys
import wasm

root = os.getenv("HTTP_PACK_ROOT")
if not root:
    raise SystemExit("HTTP_PACK_ROOT not set")

print("verify(False)  # unsigned smoke")
wasm.verify(False)
print("install_hook", root)
wasm.install_hook(root)
print("wasm.path =", wasm.path)

print("import hello  # finder → HTTP GET hello.wasm")
import hello

print("  sys.modules['hello'] =", sys.modules.get("hello"))
print("  hello.hello()  ->", hello.hello())
print("  hello.add(2,3) ->", hello.add(2, 3))
print("  hello.greet()  ->", repr(hello.greet()))
assert hello.greet() == "hello from pack py"
assert hello.hello() == 42
assert hello.add(2, 3) == 5
print("test-http OK", root)
