# pyright: reportMissingImports=false
# Signed HTTP pack smoke. Driven by `make test-http-verify`.
# Host with MICROPY_WASM_VERIFY=1 + baked MICROPY_WASM_TRUST_CA; HTTP_PACK_ROOT set.

import os
import sys
import wasm

root = os.getenv("HTTP_PACK_ROOT")
if not root:
    raise SystemExit("HTTP_PACK_ROOT required")

n = wasm.trust_count()
print("baked trust_count=", n)
if n == 0:
    ca_path = os.getenv("WASM_TRUST_CA") or os.getenv("WASM_TRUST_PUB")
    if not ca_path:
        raise SystemExit("no baked trust and WASM_TRUST_CA not set")
    print("add_trust", ca_path)
    with open(ca_path, "rb") as f:
        wasm.add_trust(f.read())

print("install_hook", root)
wasm.install_hook(root)
print("wasm.path =", wasm.path)

# With MICROPY_WASM_CONTAINERS=elf,aot,wasm the finder prefers hello.elf when present.
print("import hello  # finder → HTTP GET hello.elf or hello.wasm (+ verify)")
import hello

print("  sys.modules['hello'] =", sys.modules.get("hello"))
print("  hello.hello()  ->", hello.hello())
print("  hello.add(2,3) ->", hello.add(2, 3))
print("  hello.greet()  ->", repr(hello.greet()))
assert hello.greet() == "hello from pack py"
assert hello.hello() == 42
assert hello.add(2, 3) == 5
print("test-http-verify OK", root)
