# pyright: reportMissingImports=false
# Signed-pack smoke. Driven by `make test-verify`.
# Host built with MICROPY_WASM_VERIFY=1 and MICROPY_WASM_TRUST_CA=root.crt.der.

import os
import pymergetic.wasmmod as wasm

assert wasm.VERIFY == 1
assert wasm.verify() is True

n = wasm.trust_count()
print("baked trust_count=", n)
if n == 0:
    # Fallback when image was built without MICROPY_WASM_TRUST_CA.
    ca_path = os.getenv("WASM_TRUST_CA") or os.getenv("WASM_TRUST_PUB")
    if not ca_path:
        raise SystemExit("no baked trust and WASM_TRUST_CA not set")
    print("add_trust", ca_path)
    with open(ca_path, "rb") as f:
        wasm.add_trust(f.read())
    assert wasm.trust_count() >= 1

print("load_pack('packs/pymergetic.wasmmod_examples.hello.wasm')")
h = wasm.load_pack("packs/pymergetic.wasmmod_examples.hello.wasm")
print("  hello.hello()  ->", h.hello())
print("  hello.add(2,3) ->", h.add(2, 3))
print("  hello.greet()  ->", repr(h.greet()))
assert h.hello() == 42
assert h.add(2, 3) == 5
assert h.greet() == "hello from pack py"

print("load_pack('packs/pymergetic.wasmmod_examples.hello.elf')  # signed ELF container")
he = wasm.load_pack("packs/pymergetic.wasmmod_examples.hello.elf")
print("  hello.elf hello()  ->", he.hello())
print("  hello.elf add(2,3) ->", he.add(2, 3))
assert he.hello() == 42
assert he.add(2, 3) == 5

# Without trust, required verify must reject.
print("trust_clear(); load_pack('packs/pymergetic.wasmmod_examples.client.wasm')  # expect verify fail")
wasm.trust_clear()
try:
    wasm.load_pack("packs/pymergetic.wasmmod_examples.client.wasm")
    raise SystemExit("FAIL: load succeeded without trust")
except Exception as e:
    print("  rejected:", type(e).__name__, e)
    msg = str(e).lower()
    # Must be signature/trust rejection — not a false green from missing deps.
    if "verify" not in msg and "signature" not in msg and "trust" not in msg:
        raise SystemExit("FAIL: expected verify/signature error, got: %r" % (e,))

print("load_pack('packs/pymergetic.wasmmod_examples.hello.elf') without trust  # expect verify fail")
try:
    wasm.load_pack("packs/pymergetic.wasmmod_examples.hello.elf")
    raise SystemExit("FAIL: ELF load succeeded without trust")
except Exception as e:
    print("  rejected:", type(e).__name__, e)
    msg = str(e).lower()
    if "verify" not in msg and "signature" not in msg and "trust" not in msg:
        raise SystemExit("FAIL: expected ELF verify/signature error, got: %r" % (e,))

print("test-verify OK")
