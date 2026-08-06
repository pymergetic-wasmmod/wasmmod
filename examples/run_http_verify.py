# pyright: reportMissingImports=false
# Signed HTTP pack smoke. Driven by `make test-http-verify`.
# Host with MICROPY_WASM_VERIFY=1 + baked MICROPY_WASM_TRUST_CA; HTTP_PACK_ROOT set.

import os
import sys
import pymergetic.wasmmod as wasm

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

# Explicit signed ELF URL (fetch prefers .elf.zlib twin when present).
elf_url = root.rstrip("/") + "/pymergetic.wasmmod_examples.hello.elf"
print("load_pack", elf_url)
he = wasm.load_pack(elf_url)
print("  hello.elf hello()  ->", he.hello())
print("  hello.elf add(2,3) ->", he.add(2, 3))
assert he.hello() == 42
assert he.add(2, 3) == 5

# With MICROPY_WASM_CONTAINERS=elf,aot,wasm the finder prefers .elf when present.
print("import pymergetic.wasmmod_examples.hello  # finder → HTTP GET …hello.elf (+ verify)")
import pymergetic.wasmmod_examples.hello as hello

print("  sys.modules['pymergetic.wasmmod_examples.hello'] =", sys.modules.get("pymergetic.wasmmod_examples.hello"))
print("  hello.hello()  ->", hello.hello())
print("  hello.add(2,3) ->", hello.add(2, 3))
print("  hello.greet()  ->", repr(hello.greet()))
assert hello.greet() == "hello from pack py"
assert hello.hello() == 42
assert hello.add(2, 3) == 5
print("test-http-verify OK", root)
