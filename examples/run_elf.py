# pyright: reportMissingImports=false
# ELF container smoke (preference elf,aot,wasm). Run from examples/packs/.
# Packs/builtins resolve at runtime; stubs under typings/ for editors that see them.
import pymergetic.wasmmod as wasm

EX = "pymergetic.wasmmod_examples"

wasm.path.append(".")
h = wasm.import_wasm(f"{EX}.hello")
assert h.hello() == 42 and h.add(2, 3) == 5
p = h.__pack__
assert isinstance(p, wasm.PackModule)
assert p.kind == "elf", p.kind
assert p.name == f"{EX}.hello", p.name
assert ".elf" in p.origin, p.origin
# Naked …hello.elf (or arch-tagged) — arch infix empty when filename has none.
assert p.arch == "" or p.arch in ("x86_64", "amd64", "aarch64"), p.arch
import pymergetic.wasmmod_examples.hello.util as u

assert u.ping_code() == 7
assert u.__pack__ is p
c = wasm.import_wasm(f"{EX}.client")
assert c.use_hello() == 42
assert c.__pack__.kind == "elf"
wasm.host_set(0, lambda: 99)
hc = wasm.import_wasm(f"{EX}.hostcall")
assert hc.via_host0() == 99
assert hc.__pack__.kind == "elf" and hc.__pack__.name == f"{EX}.hostcall"
wasm.host_set(0, lambda x: x * 3)
assert hc.via_host(7) == 21
wasm.host_set(0, lambda b: len(b))
assert hc.via_buf() == 3
wasm.host_set(0, lambda b: len(b))
assert hc.via_mem() == 6
import pymergetic.wasmmod_examples.hello as _h

setattr(_h, "_elf_abs", abs)
assert hc.via_py(-7) == 7
assert hc.host_version_len() > 0
tk = wasm.import_wasm(f"{EX}.ticks")
assert tk.__pack__.kind == "elf", tk.__pack__.kind
t0 = tk.elapsed()
assert isinstance(t0, int) and t0 >= 0, t0
# Explicit Wasm twin (preference would pick …ticks.elf).
tw = wasm.load_pack(f"{EX}.ticks.wasm")
assert tw.elapsed() >= 0
hello_elf = f"{EX}.hello.elf"
hello_wasm = f"{EX}.hello.wasm"
assert wasm.locations(hello_elf, "hello")
hello_sym = next(s for s in wasm.symbols(hello_elf) if s["name"] == "hello")
assert wasm.disasm(hello_elf, hello_sym["section_index"], 0, 16)
try:
    wasm.load_pack("/tmp/wasmmod_badupy.elf")
    raise SystemExit("expected micropython.* load failure")
except RuntimeError as e:
    assert "micropython.* not supported" in str(e), e
try:
    wasm.load_pack(f"{EX}.hello.aarch64.elf")
    raise SystemExit("expected aarch64 reject on host")
except RuntimeError as e:
    assert "e_machine" in str(e), e
# Inspect API on packed ELF bytes
syms = wasm.symbols(hello_elf)
names = {s["name"] for s in syms}
assert "hello" in names and "add" in names, names
assert wasm.has_dwarf(hello_elf) is True
hello = next(s for s in syms if s["name"] == "hello")
locs = wasm.addr2line(hello_elf, hello["offset"])
assert locs and locs[0]["role"] in ("sym", "dwarf"), locs
# Wasm export listing (shared with host tools / CDN)
wsyms = wasm.symbols(hello_wasm)
wnames = {s["name"] for s in wsyms}
assert "hello" in wnames and "add" in wnames, wnames
wh = next(s for s in wsyms if s["name"] == "hello")
wa = next(s for s in wsyms if s["name"] == "add")
assert wh["size"] and wa["size"] and wh["offset"] != wa["offset"], (wh, wa)
mpy_lines = wasm.mpy_disasm(b"MP\x06\x00" + bytes(range(8)), 8)
assert mpy_lines and "mpy" in mpy_lines[0]["text"], mpy_lines
print("OK elf hello(+py)/client/hostcall/ticks+badupy+arch+inspect")
