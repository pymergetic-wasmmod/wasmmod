# This file is part of wasmmod, https://github.com/pymergetic/wasmmod
#
# The MIT License (MIT)
#
# Copyright (c) 2026 Rouven Raudzus <raudzus@pymergetic.com>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

# Wasm call-matrix smoke: host↔guest / guest↔guest × C / RS / Py.
#
#   make -C extmod/wasmmod/examples test
#   ports/unix/build-wasm/micropython extmod/wasmmod/examples/run_matrix.py
#
# Output: lang×lang tables (index numbers) + detailed call list.

import sys

try:
    import wasm  # type: ignore[import-not-found]
except ImportError:
    print("FAIL: wasm module missing — rebuild with MICROPY_PY_WASM=1")
    sys.exit(1)

try:
    HERE = __file__.rsplit("/", 1)[0]
except AttributeError:
    HERE = "extmod/wasmmod/examples"


def p(path):
    return HERE + "/" + path


# ---------------------------------------------------------------------------
# Case registry
# ---------------------------------------------------------------------------

# dir: "H" host→guest, "P" guest→host, "G" guest→guest, "S" same-pack native
# src/dst: "Py" | "C" | "RS"  (Py = host or pack-embedded Python — same language)
CASES = []  # list of dicts filled by case()


def case(dir_, src, dst, call, got, want):
    """Record + check one matrix cell. Returns the index (1-based global)."""
    ok = got == want
    idx = len(CASES) + 1
    CASES.append(
        {
            "n": idx,
            "dir": dir_,
            "src": src,
            "dst": dst,
            "call": call,
            "got": got,
            "want": want,
            "ok": ok,
        }
    )
    tag = "OK" if ok else "FAIL"
    print("  #%d  [%s] %s→%s" % (idx, dir_, src, dst))
    print("       %s" % call)
    print("       → %r  (want %r)  %s" % (got, want, tag))
    if not ok:
        raise SystemExit(1)
    return idx


def section(title):
    print()
    print("=" * 64)
    print(title)
    print("=" * 64)


def _pad(s, w):
    s = str(s)
    if len(s) >= w:
        return s
    return s + (" " * (w - len(s)))


LANGS = ("Py", "C", "RS")

# No structural n/a — every Py/C/RS × Py/C/RS cell must have a real case.
NA = {}


def _cell(dir_code, src, dst):
    idxs = [x["n"] for x in CASES if x["dir"] == dir_code and x["src"] == src and x["dst"] == dst]
    if idxs:
        return " ".join("#%d" % i for i in idxs)
    if (src, dst) in NA.get(dir_code, ()):
        return "n/a"
    return "MISSING"


def print_lang_table(title, dir_code):
    """Full Py/C/RS × Py/C/RS caller×callee table."""
    section(title)
    stub = "caller\\callee"
    widths = [max(len(stub), max(len(r) for r in LANGS))]
    for c in LANGS:
        cells = [_cell(dir_code, r, c) for r in LANGS]
        cell_w = max(len(s) for s in cells)
        widths.append(max(len(c), cell_w, 3))

    def fmt_row(parts):
        return " | ".join(_pad(part, widths[i]) for i, part in enumerate(parts))

    print(fmt_row([stub] + list(LANGS)))
    print("-+-".join("-" * w for w in widths))
    for r in LANGS:
        print(fmt_row([r] + [_cell(dir_code, r, c) for c in LANGS]))
    print("MISSING = need a case (bug).")


DIR_NAMES = {
    "H": "host → guest",
    "P": "guest → host",
    "G": "guest → guest",
    "S": "same-pack",
}


def print_catalog(dir_code):
    """Print call list for one direction (indices matching that table)."""
    rows = [x for x in CASES if x["dir"] == dir_code]
    print()
    print("Calls — %s" % DIR_NAMES.get(dir_code, dir_code))
    print("-" * 64)
    if not rows:
        print("(none)")
        return
    for x in rows:
        print("#%-3d  %s → %s" % (x["n"], x["src"], x["dst"]))
        print("      %s" % x["call"])
        print("      result %r%s" % (x["got"], "" if x["ok"] else "  FAIL"))
        print()


# ---------------------------------------------------------------------------
# Setup
# ---------------------------------------------------------------------------

section("Setup")
_MODE_NAME = {
    1: "Interp",
    2: "Fast_JIT",
    3: "LLVM_JIT",
    4: "Multi_Tier_JIT",
}
print(
    "Host: VERIFY=%d  AOT=%d  JIT=%d  FAST_JIT=%d  MODE=%s(%d)"
    % (
        wasm.VERIFY,
        wasm.AOT,
        wasm.JIT,
        wasm.FAST_JIT,
        _MODE_NAME.get(wasm.MODE, "?"),
        wasm.MODE,
    )
)

print("Load peer packs (guest→guest needs them registered first)")
h = wasm.load_pack(p("packs/hello.wasm"))
m = wasm.load_pack(p("packs/mixed.wasm"))
print("  sys.modules: hello, mixed")


def host_double(x):
    return x * 2


def host_const():
    return 99


def host_i64(x):
    return x + 100


def host_f32(x):
    return x * 3


def host_f64(x):
    return x + 0.25


def host_bytes(data):
    if data == b"ping":
        return 4
    if data == b"pong":
        return 5
    return -1


def host_obj(obj):
    return obj * 3


wasm.host_clear()
wasm.mem_clear()
wasm.handle_clear()
wasm.host_set(0, host_double)
wasm.host_set(1, host_const)
wasm.host_set(2, host_i64)
wasm.host_set(3, host_f32)
wasm.host_set(4, host_f64)
wasm.host_set(5, host_bytes)
wasm.host_set(6, host_obj)
# Host C / RS fun-objs (guest→host → C / RS)
wasm.host_set(7, wasm.host_c_triple)
wasm.host_set(8, wasm.host_rs_triple)
print("  host slots 0..8: py*7 + host_c_triple + host_rs_triple")

# Host modules for pack-Python → host Py / C / RS.
import sys


class _HostApi:
    def triple(self, x):
        return x * 3


class _HostC:
    triple = wasm.host_c_triple


class _HostRS:
    triple = wasm.host_rs_triple


sys.modules["hostapi"] = _HostApi()  # type: ignore[assignment]
sys.modules["hostc"] = _HostC()  # type: ignore[assignment]
sys.modules["hostrs"] = _HostRS()  # type: ignore[assignment]
print("  sys.modules: hostapi, hostc, hostrs")

b = wasm.load_pack(p("packs/bridge.wasm"))
print("  sys.modules: bridge")

# pack-embedded Python (runtime import)
import hello.util  # type: ignore[import-not-found]
import hello.util.extra  # type: ignore[import-not-found]

# ---------------------------------------------------------------------------
# H — Host → Guest  (Python / pack-Py calls into C / RS / pack-Py)
# ---------------------------------------------------------------------------

section("Run cases")

case("H", "Py", "C", "hello.hello()  [C export i32]", h.hello(), 42)
case("H", "Py", "C", "hello.add(2, 3)  [C export i32,i32→i32]", h.add(2, 3), 5)
case(
    "H",
    "Py",
    "C",
    "bridge.via_i64(10) / via_f32(1.5) / via_f64(1.0) / add3 / scale_add_f64",
    (
        b.via_i64(10),
        b.via_f32(1.5),
        b.via_f64(1.0),
        b.add3(1, 2, 3),
        b.scale_add_f64(2.0, 3.0, 0.5),
    ),
    (11, 3.0, 1.5, 6, 6.5),
)
case(
    "H",
    "Py",
    "RS",
    "bridge.rs_square(5)  [Rust export i32→i32]",
    b.rs_square(5),
    25,
)
case(
    "H",
    "Py",
    "RS",
    "bridge.rs_via_i64/f32/f64 + rs_add3 + rs_scale_add_f64",
    (
        b.rs_via_i64(10),
        b.rs_via_f32(1.5),
        b.rs_via_f64(1.0),
        b.rs_add3(4, 5, 6),
        b.rs_scale_add_f64(2.0, 3.0, 0.5),
    ),
    (11, 3.0, 1.5, 15, 6.5),
)
case(
    "H",
    "Py",
    "Py",
    "hello.util.ping() + hello.util.extra.twice(21)  [embedded .py]",
    (hello.util.ping(), hello.util.extra.twice(21)),
    ("util ping", 42),
)
case(
    "H",
    "Py",
    "Py",
    "bridge.ping()  [embedded pack Python]",
    b.ping(),
    "bridge-py",
)
_wm = b.__pack__
_off = _wm.memory_alloc(4)
_wm.memory_write(_off, b"ABCD")
_mem_got = _wm.memory_read(_off, 4)
_wm.memory_free(_off)
case(
    "H",
    "Py",
    "C",
    "b.__pack__.memory_alloc/write/read  [host linear memory API]",
    _mem_got,
    b"ABCD",
)

_ck = wasm.mem_alloc(b"\x01\x02\x03\x04")
_ck_sum = b.via_mem_out(_ck)
_ck_bytes = wasm.mem_get(_ck)
wasm.mem_free(_ck)
case(
    "H",
    "Py",
    "C",
    "wasm.mem_alloc(bytes) → bridge.via_mem_out(cookie) → sum=%d get=%r" % (_ck_sum, _ck_bytes),
    _ck_sum,
    10,
)

# ---------------------------------------------------------------------------
# S — Same-pack native (C ↔ RS inside one .wasm; still reached from host)
# ---------------------------------------------------------------------------

case(
    "S",
    "C",
    "RS",
    "mixed.mixed_answer()  [C shim → rs_answer]",
    m.mixed_answer(),
    42,
)
case(
    "S",
    "C",
    "RS",
    "bridge.via_rs(6)  [C wrapper → rs_square]",
    b.via_rs(6),
    36,
)
case(
    "S",
    "C",
    "RS",
    "mixed.mixed_i64(10)  [C shim → rs_i64_answer]",
    m.mixed_i64(10),
    17,
)
case(
    "S",
    "Py",
    "RS",
    "bridge.via_native(4)  [pack py → via_rs → rs_square]",
    b.via_native(4),
    16,
)
case(
    "S",
    "Py",
    "C",
    "bridge.via_native_rich(10)  [pack py → via_i64/f32/add3/scale_add (C)]",
    b.via_native_rich(10),
    26,
)
case(
    "S",
    "C",
    "C",
    "bridge.via_c_self(5)  [C → add3(5,5,5) same pack]",
    b.via_c_self(5),
    15,
)
case(
    "S",
    "RS",
    "RS",
    "bridge.rs_via_square(6)  [RS → rs_square same pack]",
    b.rs_via_square(6),
    36,
)
case(
    "S",
    "RS",
    "C",
    "bridge.rs_via_add3(10)  [RS → add3(10,1,2) same pack]",
    b.rs_via_add3(10),
    13,
)
case(
    "S",
    "Py",
    "Py",
    "bridge.via_py_self()  [pack py → ping()]",
    b.via_py_self(),
    "bridge-py",
)
case(
    "S",
    "C",
    "Py",
    "bridge.via_pack_py()  [C → call0_py(bridge.ping_code)]",
    b.via_pack_py(),
    8,
)
case(
    "S",
    "RS",
    "Py",
    "bridge.rs_via_pack_py()  [RS → call0_py(bridge.ping_code)]",
    b.rs_via_pack_py(),
    8,
)

# ---------------------------------------------------------------------------
# P — Guest → Host  (C / RS / pack-Py → host Py / C / RS)
# ---------------------------------------------------------------------------

case(
    "P",
    "Py",
    "Py",
    "bridge.via_host_api(7)  [pack py → hostapi.triple]",
    b.via_host_api(7),
    21,
)
case(
    "P",
    "Py",
    "C",
    "bridge.via_host_c_api(7)  [pack py → hostc.triple (C fun-obj)]",
    b.via_host_c_api(7),
    21,
)
case(
    "P",
    "Py",
    "RS",
    "bridge.via_host_rs_api(7)  [pack py → hostrs.triple (RS fun-obj)]",
    b.via_host_rs_api(7),
    21,
)
case(
    "P",
    "C",
    "Py",
    "bridge.via_host(7) → call_i32(0,7) → host_double → 14",
    b.via_host(7),
    14,
)
case(
    "P",
    "C",
    "Py",
    "bridge.via_host0() → call0_i32(1) → host_const → 99",
    b.via_host0(),
    99,
)
case(
    "P",
    "C",
    "Py",
    "bridge.via_host_i64/f32/f64 → call_i64/f32/f64",
    (b.via_host_i64(5), b.via_host_f32(2.0), b.via_host_f64(1.0)),
    (105, 6.0, 1.25),
)
case(
    "P",
    "C",
    "Py",
    "bridge.via_buf() → call_buf(5, off, 4) → host_bytes(b'ping')",
    b.via_buf(),
    4,
)
case(
    "P",
    "C",
    "Py",
    "bridge.via_mem() → mem_alloc+copy_in + call_mem → host_bytes(b'pong')",
    b.via_mem(),
    5,
)
_hnd = wasm.handle_register(11)
_hnd_c = b.via_handle(_hnd)
wasm.handle_free(_hnd)
case(
    "P",
    "C",
    "Py",
    "handle_register(11) → bridge.via_handle → call_obj → host_obj*3",
    _hnd_c,
    33,
)
case(
    "P",
    "RS",
    "Py",
    "bridge.rs_via_host(7) → call_i32(0,7) → host_double",
    b.rs_via_host(7),
    14,
)
case(
    "P",
    "RS",
    "Py",
    "bridge.rs_via_host_i64/f32/f64 → call_i64/f32/f64",
    (b.rs_via_host_i64(5), b.rs_via_host_f32(2.0), b.rs_via_host_f64(1.0)),
    (105, 6.0, 1.25),
)
case(
    "P",
    "RS",
    "Py",
    "bridge.rs_via_buf() → call_buf → host_bytes(b'ping')",
    b.rs_via_buf(),
    4,
)
case(
    "P",
    "RS",
    "Py",
    "bridge.rs_via_mem() → call_mem → host_bytes(b'pong')",
    b.rs_via_mem(),
    5,
)
_hnd2 = wasm.handle_register(11)
_hnd_rs = b.rs_via_handle(_hnd2)
wasm.handle_free(_hnd2)
case(
    "P",
    "RS",
    "Py",
    "handle_register(11) → bridge.rs_via_handle → call_obj",
    _hnd_rs,
    33,
)
case(
    "P",
    "C",
    "C",
    "bridge.via_host_c(7) → call_i32(7) → host_c_triple",
    b.via_host_c(7),
    21,
)
case(
    "P",
    "C",
    "RS",
    "bridge.via_host_rs(7) → call_i32(8) → host_rs_triple",
    b.via_host_rs(7),
    21,
)
case(
    "P",
    "RS",
    "C",
    "bridge.rs_via_host_c(7) → call_i32(7) → host_c_triple",
    b.rs_via_host_c(7),
    21,
)
case(
    "P",
    "RS",
    "RS",
    "bridge.rs_via_host_rs(7) → call_i32(8) → host_rs_triple",
    b.rs_via_host_rs(7),
    21,
)

# ---------------------------------------------------------------------------
# G — Guest → Guest  (forwarders across packs)
# ---------------------------------------------------------------------------

case(
    "G",
    "C",
    "C",
    "bridge.via_hello() → import hello.hello → C peer",
    b.via_hello(),
    42,
)
case(
    "G",
    "RS",
    "C",
    "bridge.rs_via_hello() → #[link(wasm_import_module=\"hello\")] hello()",
    b.rs_via_hello(),
    42,
)
case(
    "G",
    "C",
    "C",
    "bridge.via_loader_hello() → wasmmod.call_i32('hello','hello')",
    b.via_loader_hello(),
    42,
)
case(
    "G",
    "RS",
    "C",
    "bridge.rs_via_loader_hello() → wasmmod.call_i32('hello','hello')",
    b.rs_via_loader_hello(),
    42,
)
case(
    "H",
    "Py",
    "C",
    "bridge.via_loader_version_len() == len(wasm.version)",
    b.via_loader_version_len(),
    len(wasm.version),
)
case(
    "H",
    "Py",
    "RS",
    "bridge.rs_via_loader_version_len() == len(wasm.version)",
    b.rs_via_loader_version_len(),
    len(wasm.version),
)
case(
    "H",
    "Py",
    "C",
    "bridge.via_loader_mode() == wasm.MODE",
    b.via_loader_mode(),
    int(wasm.MODE),
)
case(
    "H",
    "Py",
    "C",
    "bridge.via_loader_verify() == int(bool(wasm.verify()))",
    b.via_loader_verify(),
    1 if wasm.verify() else 0,
)
case(
    "H",
    "Py",
    "C",
    "bridge.via_loader_trust_count() == wasm.trust_count()",
    b.via_loader_trust_count(),
    int(wasm.trust_count()),
)
case(
    "G",
    "C",
    "RS",
    "bridge.via_mixed() → import mixed.mixed_answer → C→RS peer",
    b.via_mixed(),
    42,
)
case(
    "G",
    "C",
    "RS",
    "bridge.via_mixed_i64(10) → import mixed.mixed_i64 → C→RS peer",
    b.via_mixed_i64(10),
    17,
)
case(
    "G",
    "RS",
    "RS",
    "bridge.rs_via_mixed_i64(10) → Rust import mixed.mixed_i64",
    b.rs_via_mixed_i64(10),
    17,
)

c = wasm.load_pack(p("packs/client.wasm"))
case(
    "G",
    "C",
    "C",
    "client.use_hello() → import hello.hello  [separate client pack]",
    c.use_hello(),
    42,
)
wasm.unload("client")

case(
    "G",
    "Py",
    "C",
    "bridge.via_peer_hello()  [pack py import hello → C export]",
    b.via_peer_hello(),
    42,
)
case(
    "G",
    "Py",
    "RS",
    "bridge.via_peer_mixed_i64(10)  [pack py import mixed → mixed_i64]",
    b.via_peer_mixed_i64(10),
    17,
)
case(
    "G",
    "Py",
    "Py",
    "bridge.via_peer_util()  [pack py → hello.util.ping peer py]",
    b.via_peer_util(),
    "util ping",
)
case(
    "G",
    "C",
    "Py",
    "bridge.via_peer_py()  [C → call0_py(hello.util.ping_code)]",
    b.via_peer_py(),
    7,
)
case(
    "G",
    "RS",
    "Py",
    "bridge.rs_via_peer_py()  [RS → call0_py(hello.util.ping_code)]",
    b.rs_via_peer_py(),
    7,
)

# Combos (span multiple directions — tagged host→guest entry point)
case(
    "H",
    "Py",
    "C",
    "bridge.matrix(3) = rs_square + hello + mixed_answer + host_double  [=99]",
    b.matrix(3),
    99,
)
case(
    "H",
    "Py",
    "C",
    "bridge.matrix_rich(5) = rs_add3 + host_i64 + scale_add_f64 + mixed_i64  [=129]",
    b.matrix_rich(5),
    129,
)

# Host C / RS → guest (real host-side C/RS calling into packs)
case(
    "H",
    "C",
    "C",
    "wasm.c_call('hello','hello')  [host C → guest C export]",
    wasm.c_call("hello", "hello"),
    42,
)
case(
    "H",
    "C",
    "RS",
    "wasm.c_call('bridge','rs_square',5)  [host C → guest RS]",
    wasm.c_call("bridge", "rs_square", 5),
    25,
)
case(
    "H",
    "C",
    "Py",
    "wasm.c_call_attr('hello.util','ping')  [host C → pack Py]",
    wasm.c_call_attr("hello.util", "ping"),
    "util ping",
)
case(
    "H",
    "RS",
    "C",
    "wasm.rs_call('hello','hello')  [host RS → guest C]",
    wasm.rs_call("hello", "hello"),
    42,
)
case(
    "H",
    "RS",
    "RS",
    "wasm.rs_call('bridge','rs_square',5)  [host RS → guest RS]",
    wasm.rs_call("bridge", "rs_square", 5),
    25,
)
case(
    "H",
    "RS",
    "Py",
    "wasm.rs_call_attr('bridge','ping')  [host RS → pack Py]",
    wasm.rs_call_attr("bridge", "ping"),
    "bridge-py",
)

# ---------------------------------------------------------------------------
# Tables + catalog
# ---------------------------------------------------------------------------

section("Summary (table + calls per direction)")
print("Every table is full Py/C/RS × Py/C/RS (caller × callee).")
print("Py = host or pack-embedded Python.  MISSING = bug.")

missing = []
for dir_code, title in (
    ("H", "TABLE  host → guest"),
    ("P", "TABLE  guest → host"),
    ("G", "TABLE  guest → guest"),
    ("S", "TABLE  same-pack (inside one .wasm)"),
):
    print_lang_table(title, dir_code)
    print_catalog(dir_code)
    for r in LANGS:
        for c in LANGS:
            if _cell(dir_code, r, c) == "MISSING":
                missing.append("%s %s→%s" % (dir_code, r, c))

if missing:
    print("FAIL: empty matrix cells:")
    for m in missing:
        print("  ", m)
    raise SystemExit(1)

section("Cleanup")
wasm.unload("bridge")
wasm.unload("mixed")
wasm.unload("hello")
wasm.host_clear()
wasm.mem_clear()
wasm.handle_clear()
print("%d cases OK" % len(CASES))
print()
print("ALL STEPS OK")
