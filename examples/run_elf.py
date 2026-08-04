# pyright: reportMissingImports=false
# ELF container smoke (preference elf,aot,wasm). Run from examples/packs/.
# Packs/builtins resolve at runtime; stubs under typings/ for editors that see them.
import wasm

wasm.path.append(".")
h = wasm.import_wasm("hello")
assert h.hello() == 42 and h.add(2, 3) == 5
p = h.__pack__
assert isinstance(p, wasm.PackModule)
assert p.kind == "elf", p.kind
assert p.name == "hello", p.name
assert ".elf" in p.origin, p.origin
# Naked hello.elf (or arch-tagged) — arch infix empty when filename has none.
assert p.arch == "" or p.arch in ("x86_64", "amd64", "aarch64"), p.arch
import hello.util as u

assert u.ping_code() == 7
assert u.__pack__ is p
c = wasm.import_wasm("client")
assert c.use_hello() == 42
assert c.__pack__.kind == "elf"
wasm.host_set(0, lambda: 99)
hc = wasm.import_wasm("hostcall")
assert hc.via_host0() == 99
assert hc.__pack__.kind == "elf" and hc.__pack__.name == "hostcall"
wasm.host_set(0, lambda x: x * 3)
assert hc.via_host(7) == 21
wasm.host_set(0, lambda b: len(b))
assert hc.via_buf() == 3
wasm.host_set(0, lambda b: len(b))
assert hc.via_mem() == 6
import hello as _h

setattr(_h, "_elf_abs", abs)
assert hc.via_py(-7) == 7
assert hc.host_version_len() > 0
try:
    wasm.load_pack("/tmp/wasmmod_badupy.elf")
    raise SystemExit("expected micropython.* load failure")
except RuntimeError as e:
    assert "micropython.* not supported" in str(e), e
try:
    wasm.load_pack("hello.aarch64.elf")
    raise SystemExit("expected aarch64 reject on host")
except RuntimeError as e:
    assert "e_machine" in str(e), e
print("OK elf hello(+py)/client/hostcall+badupy+arch")
