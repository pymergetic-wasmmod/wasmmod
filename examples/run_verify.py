# pyright: reportMissingImports=false
# Signed-pack smoke. Driven by `make test-verify`.
# Expects WASM_TRUST_CA (root CA DER) env; host built with MICROPY_WASM_VERIFY=1.

import os
import wasm

ca_path = os.getenv("WASM_TRUST_CA") or os.getenv("WASM_TRUST_PUB")
if not ca_path:
    raise SystemExit("WASM_TRUST_CA not set")

with open(ca_path, "rb") as f:
    wasm.add_trust(f.read())

assert wasm.VERIFY == 1
assert wasm.verify() is True

h = wasm.load_pack("packs/hello.wasm")
assert h.hello() == 42
assert h.add(2, 3) == 5
assert h.greet() == "hello from pack py"

# Without trust, required verify must reject.
wasm.trust_clear()
try:
    wasm.load_pack("packs/client.wasm")
    raise SystemExit("FAIL: load succeeded without trust")
except Exception as e:
    print("reject-without-trust OK", type(e).__name__)

print("test-verify OK", "VERIFY=", wasm.VERIFY, "ca=", ca_path)
