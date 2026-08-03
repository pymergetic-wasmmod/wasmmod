# This file is part of the MicroPython project, http://micropython.org/
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

# Embedded Python in the bridge pack (Py → pack py → native).

def ping():
    return "bridge-py"


def ping_code():
    """Int-returning pack Python for native→Py (call0_py)."""
    return 8


def via_native(x):
    """Python in the pack calling this pack's native export."""
    import bridge  # type: ignore[import-not-found]
    return bridge.via_rs(x)


def via_native_rich(x):
    """Pack Python exercising richer numeric native exports."""
    import bridge  # type: ignore[import-not-found]
    i = bridge.via_i64(x)
    f = bridge.via_f32(1.5)
    d = bridge.scale_add_f64(2.0, 3.0, 0.5)
    a = bridge.add3(1, 2, 3)
    return i + int(f) + int(d) + a  # (x+1) + 3 + 6 + 6 = x+16


def via_peer_hello():
    """Pack Python → peer pack C (sys.modules guest→guest)."""
    import hello  # type: ignore[import-not-found]
    return hello.hello()


def via_peer_mixed_i64(x):
    """Pack Python → peer pack mixed (C→RS) export."""
    import mixed  # type: ignore[import-not-found]
    return mixed.mixed_i64(x)


def via_py_self():
    """Same-pack Py → Py."""
    return ping()


def via_peer_util():
    """Guest→guest Py → Py (peer pack embedded python)."""
    import hello.util  # type: ignore[import-not-found]
    return hello.util.ping()


def via_host_api(x):
    """Guest→host Py → Py (host-injected module)."""
    import hostapi  # type: ignore[import-not-found]
    return hostapi.triple(x)


def via_host_c_api(x):
    """Guest→host Py → host C fun-obj."""
    import hostc  # type: ignore[import-not-found]
    return hostc.triple(x)


def via_host_rs_api(x):
    """Guest→host Py → host RS fun-obj."""
    import hostrs  # type: ignore[import-not-found]
    return hostrs.triple(x)
