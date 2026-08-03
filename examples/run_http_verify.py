# pyright: reportMissingImports=false
# Signed HTTP pack smoke. Driven by `make test-http-verify`.
# Expects HTTP_PACK_ROOT + WASM_TRUST_CA; host with MICROPY_WASM_VERIFY=1.

import os
import wasm

root = os.getenv("HTTP_PACK_ROOT")
ca_path = os.getenv("WASM_TRUST_CA") or os.getenv("WASM_TRUST_PUB")
if not root or not ca_path:
    raise SystemExit("HTTP_PACK_ROOT and WASM_TRUST_CA required")

with open(ca_path, "rb") as f:
    wasm.add_trust(f.read())

wasm.install_hook(root)
import hello

assert hello.greet() == "hello from pack py"
assert hello.hello() == 42
assert hello.add(2, 3) == 5
print("test-http-verify OK", root)
