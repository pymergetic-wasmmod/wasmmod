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

"""Pretty host demo for README screenshots. Run from examples/ with packs built."""

import sys

try:
    import wasm
except ImportError:
    print("error: build host with MICROPY_PY_WASM=1", file=sys.stderr)
    raise SystemExit(1)

HERE = __file__.rsplit("/", 1)[0] if "/" in __file__ else "."


def _p(rel):
    return HERE + "/" + rel


def _load(name):
    path = _p(name + "/" + name + ".wasm")
    try:
        wasm.load_pack(path, name)
    except OSError as e:
        print("error: missing %s — run: make packs (%s)" % (path, e), file=sys.stderr)
        raise SystemExit(1)


def _show(label, value):
    print(">>>", label)
    print(repr(value) if isinstance(value, str) else value)


def main():
    wasm.install_hook()
    for n in ("hello", "mixed", "bridge"):
        _load(n)

    import hello  # noqa: F401
    import mixed  # noqa: F401
    import bridge  # noqa: F401

    print("# wasmmod demo — C + Rust + pack Python, guest→guest")
    print()
    _show("hello.greet()", hello.greet())
    _show("hello.answer()           # pack Py → C", hello.answer())
    _show("mixed.mixed_answer()     # C shim → Rust", mixed.mixed_answer())
    _show("bridge.via_rs(6)         # same-pack C → RS", bridge.via_rs(6))
    _show("bridge.via_hello()       # guest → guest", bridge.via_hello())
    _show("bridge.via_peer_hello()  # pack Py → peer C", bridge.via_peer_hello())
    print()
    print("# wasm.MODE=%s  AOT=%s  JIT=%s  FAST_JIT=%s" % (
        wasm.MODE, wasm.AOT, wasm.JIT, wasm.FAST_JIT))


if __name__ == "__main__":
    main()
