# ELF container smoke (preference elf,aot,wasm). Run from examples/packs/.
import wasm

wasm.path.append(".")
h = wasm.import_wasm("hello")
assert h.hello() == 42 and h.add(2, 3) == 5
import hello.util as u

assert u.ping_code() == 7
c = wasm.import_wasm("client")
assert c.use_hello() == 42
wasm.host_set(0, lambda: 99)
hc = wasm.import_wasm("hostcall")
assert hc.via_host0() == 99
wasm.host_set(0, lambda x: x * 3)
assert hc.via_host(7) == 21
wasm.host_set(0, lambda b: len(b))
assert hc.via_buf() == 3
wasm.host_set(0, lambda b: len(b))
assert hc.via_mem() == 6
import hello as _h

_h._elf_abs = abs
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
