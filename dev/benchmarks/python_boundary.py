"""MicroPython-only binding benchmark; run after building the host fixtures.

Usage: micropython python_boundary.py native.so fixture.wasm fixture.aot symbol
The native reference uses MicroPython's libffi binding, NOT a static C module.
Wasm/AOT use wasmmod.call (including name/signature lookup on every call).
All paths run the same compiled two-i32 kernel. No production APIs are added.
Raw JSON lines are printed with the PYBENCH prefix for a host runner to capture.
Loading, network, verification, memory-copy throughput and firmware are not timed.
"""
import gc
import json
import sys
import time
import pymergetic.wasmmod as wasm

# Built into the selected MicroPython host binary, not a CPython package.
ffi = __import__("ffi")


def emit(value):
    print("PYBENCH " + json.dumps(value))


def reference_mix(state, work):
    # Independent unsigned arithmetic oracle for overhead_workload.c.
    for _ in range(work):
        state ^= state >> 16
        state = (state * 0x7feb352d) & 0xffffffff
        state ^= state >> 15
        state = (state * 0x846ca68b) & 0xffffffff
        state ^= state >> 16
    return state


def measure(fn, work, count):
    # MicroPython adds these clocks to time; desktop Python's type stubs do not.
    ticks_us = getattr(time, "ticks_us")
    ticks_diff = getattr(time, "ticks_diff")
    x = 12345
    begin = ticks_us()
    for _ in range(count):
        # Keep subsequent inputs in signed i32 range on all bindings.
        x = fn(x, work) & 0x7fffffff
    elapsed = ticks_diff(ticks_us(), begin)
    return elapsed, x


def main():
    if len(sys.argv) != 5:
        raise ValueError("expected native.so fixture.wasm fixture.aot symbol")
    lib = ffi.open(sys.argv[1])
    symbol = sys.argv[4]
    if symbol != "overhead_mix":
        raise ValueError("oracle is specific to overhead_mix")
    native = lib.func("I", symbol, "II")
    # Unsigned local fixtures only. This is a call-path measurement, not trust proof.
    wasm.verify(False)
    modules = []
    for path, name in ((sys.argv[2], "bench.py.interp"), (sys.argv[3], "bench.py.aot")):
        with open(path, "rb") as stream:
            modules.append(wasm.load(stream.read(), name))

    def interp(seed, work):
        return wasm.call("bench.py.interp", symbol, (seed, work))

    def aot(seed, work):
        return wasm.call("bench.py.aot", symbol, (seed, work))

    # Equal Python wrapper depth; native additionally traverses libffi.
    def native_ffi(seed, work):
        return native(seed, work)

    paths = [("native_ffi", native_ffi), ("wasmmod_interp_call", interp), ("wasmmod_aot_call", aot)]
    cases = []
    for work in (0, 1, 64, 4096):
        for seed in (0, 1, 12345, 0x7fffffff):
            expected = reference_mix(seed, work)
            for name, fn in paths:
                actual = fn(seed, work) & 0xffffffff
                if actual != expected:
                    raise ValueError(("incorrect result", name, seed, work, actual, expected))
        for name, fn in paths:
            measure(fn, work, 32)
            count = 32
            while True:
                elapsed = 1 << 30
                for _ in range(3):
                    gc.collect()
                    duration, checksum = measure(fn, work, count)
                    elapsed = min(elapsed, duration)
                if elapsed >= 30000:
                    break
                count *= 2
                if count > 16777216:
                    raise ValueError("timer/calibration did not converge")
            # Reference check for the exact timed iteration count, outside timing.
            expected_checksum = measure(native_ffi, work, count)[1]
            cases.append((name, fn, work, count, expected_checksum))

    emit({"kind": "metadata", "implementation": repr(sys.implementation),
          "clock": "ticks_us/ticks_diff", "gc": "enabled; collect before each sample",
          "native_baseline": "MicroPython libffi, not static C module",
          "fixtures": sys.argv[1:4], "symbol": symbol,
          "python_loop_and_result_mask_included": True, "samples": 7})
    for sample in range(7):
        for index in range(len(cases)):
            name, fn, work, count, expected = cases[(index + sample) % len(cases)]
            gc.collect()
            elapsed, checksum = measure(fn, work, count)
            if elapsed <= 0 or checksum != expected:
                raise ValueError(("sample failed", name, work, elapsed, checksum, expected))
            emit({"kind": "sample", "path": name, "work": work,
                  "sample": sample, "calls": count, "elapsed_us": elapsed,
                  "ns_per_call": elapsed * 1000.0 / count, "checksum": checksum})
    for name in ("bench.py.interp", "bench.py.aot"):
        wasm.unload(name)


main()
