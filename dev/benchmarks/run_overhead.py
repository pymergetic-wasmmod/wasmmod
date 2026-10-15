#!/usr/bin/env python3
"""Build and run the focused Linux host C/registry/WAMR overhead benchmark."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "target" / "overhead-bench"


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""Scope and interpretation:
  One separately compiled uint32 dependent mix source: native clang -O2
  -march=native (no LTO), wasm32 clang -O2, then this cargo build's wamrc -O2
  (default host CPU autodetection, recorded in compiler stdout) with explicit
  memory AND stack bounds checks. Uses actual release Rust registry/loader.
  Native direct/cached pointer versus cached registry trampoline/name lookup
  for WAMR fast interpreter and AOT. Work 0,1,64,4096; 7 rotated/interleaved
  samples; CLOCK_MONOTONIC_RAW; warm-up and per-case calibration; checked live
  checksums; no empty-baseline subtraction. Timings include dependency/checksum
  bookkeeping and registry marshaling/status checks where applicable.
  Load timing is load+instantiate+register from memory in a warm process,
  excluding init, disk/network, signature verification and cold startup.
  This scalar kernel is NOT a memory/SIMD/application benchmark, not direct
  WAMR C API cost, not Python overhead, and not a security/performance proof
  for arbitrary workloads. Bounds options do not create a memory workload.
  Pins the harness to one allowed CPU (default: first allowed); records
  affinity, commands, versions, commits/dirty state, sizes, raw samples and
  median/min/max in target/overhead-bench/report.json. Does not control CPU
  frequency, turbo, thermals, interrupts or other processes. No silent skips.
  Requires Linux, cargo, clang with wasm32/wasm-ld, system LLVM for wamrc,
  and the normal release crate build dependencies. No production edits.
""")
    parser.add_argument("--clang", default="clang", help="clang executable (default: clang)")
    parser.add_argument("--cpu", type=int, help="allowed Linux CPU to pin (default: first allowed)")
    parser.add_argument("--sample-ms", type=float, default=40, help="calibration target 30..50 ms per case (default: 40)")
    args = parser.parse_args()
    if platform.system() != "Linux":
        parser.error("this benchmark requires Linux CLOCK_MONOTONIC_RAW and CPU affinity")
    if not 30 <= args.sample_ms <= 50:
        parser.error("--sample-ms must be 30..50")
    allowed = sorted(os.sched_getaffinity(0))
    cpu = allowed[0] if args.cpu is None else args.cpu
    if cpu not in allowed:
        parser.error(f"CPU {cpu} not in allowed affinity {allowed}")
    OUT.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": 1, "status": "running",
        "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "commands": [], "platform": platform.platform(),
        "python": sys.version, "affinity_before": allowed, "harness_affinity": [cpu],
        "cpuinfo": Path("/proc/cpuinfo").read_text(),
        "sample_target_ms": args.sample_ms, "repetitions": 7,
        "clock": "CLOCK_MONOTONIC_RAW", "subtract_empty_baseline": False,
        "scope": parser.epilog,
        "environment": {k: v for k, v in os.environ.items() if k.startswith(("CARGO_", "RUST", "LLVM", "CC", "CFLAGS", "LDFLAGS"))},
    }
    report_path = OUT / "report.json"
    # Keep earlier attempts, including failures; never silently replace evidence.
    if report_path.exists():
        report_path.rename(OUT / f"report-previous-{time.time_ns()}.json")
    report["loadavg_before"] = list(os.getloadavg())

    def save():
        report_path.write_text(json.dumps(report, indent=2) + "\n")

    def run(command, cwd=ROOT, ok_codes=(0,)):
        command = [str(x) for x in command]
        print("+ " + shlex.join(command), flush=True)
        start = time.monotonic()
        p = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        report["commands"].append({"argv": command, "cwd": str(cwd), "returncode": p.returncode,
            "elapsed_s": time.monotonic()-start, "stdout": p.stdout, "stderr": p.stderr})
        save()
        if p.returncode not in ok_codes:
            raise RuntimeError(f"command failed ({p.returncode}): {shlex.join(command)}\n{p.stdout}\n{p.stderr}")
        return p.stdout + (p.stderr if not p.stdout else "")

    def revision(path):
        return {"path": str(path), "commit": run(["git", "rev-parse", "HEAD"], cwd=path).strip(),
                "dirty_status": run(["git", "status", "--short", "--untracked-files=all"], cwd=path)}

    try:
        report["repositories"] = {"wasmmod": revision(ROOT), "metalpython": revision(ROOT.parents[1]),
            "wamr": revision(ROOT / "third_party" / "wamr")}
        run(["git", "check-ignore", str(OUT / "report.json")])
        report["versions"] = {"clang": run([args.clang, "--version"]),
            "cargo": run(["cargo", "--version"]), "rustc": run(["rustc", "--version", "--verbose"])}
        # Existing libc allocator/panic feature is required by this C staticlib host.
        cargo_output = run(["cargo", "build", "--release", "--features", "upy-host", "--message-format=json"])
        messages = [json.loads(line) for line in cargo_output.splitlines() if line.startswith("{")]
        wamrcs = [value for msg in messages if msg.get("reason") == "build-script-executed"
                   for key, value in msg.get("env", []) if key == "WAMRC_PATH"]
        libraries = [Path(name) for msg in messages if msg.get("reason") == "compiler-artifact"
                     and msg.get("target", {}).get("name") == "pymergetic_wasmmod"
                     for name in msg.get("filenames", []) if name.endswith(".a")]
        if len(set(wamrcs)) != 1 or len(set(libraries)) != 1:
            raise RuntimeError(f"cannot discover exact cargo wamrc/staticlib: {wamrcs}, {libraries}")
        wamrc, library = wamrcs[0], libraries[0]
        report["wamrc_path"] = wamrc
        report["versions"]["wamrc"] = run([wamrc, "--version"])
        source = ROOT / "dev" / "benchmarks" / "overhead_workload.c"
        host = ROOT / "dev" / "benchmarks" / "overhead_host.c"
        obj, wasm, aot, exe = [OUT / x for x in ("workload.o", "workload.wasm", "workload.aot", "overhead_host")]
        common = [args.clang, "-O2", "-fno-lto", "-std=c11", "-Wall", "-Wextra", "-Werror"]
        run(common + ["-march=native", "-c", source, "-o", obj])
        run(common + ["--target=wasm32", "-nostdlib", "-mno-simd128", "-mno-reference-types",
            "-mno-bulk-memory", "-Wl,--no-entry", "-Wl,--export=overhead_mix",
            "-Wl,--initial-memory=131072", source, "-o", wasm])
        run([wamrc, "--opt-level=2", "--bounds-checks=1", "--stack-bounds-checks=1", "-o", aot, wasm])
        run(common + ["-march=native", "-I", ROOT / "src", host, obj, library, "-pthread", "-ldl", "-lm", "-o", exe])
        artifacts = [source, host, Path(__file__), obj, wasm, aot, exe, library, Path(wamrc)]
        report["artifacts"] = {str(p): {"bytes": p.stat().st_size,
            "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in artifacts}
        # Preserve WAMR's concrete configured build flags, not only build.rs intent.
        caches = list(Path(wamrc).parents[2].glob("vmlib/build/CMakeCache.txt"))
        report["wamr_cmake_caches"] = {str(p): p.read_text() for p in caches}
        os.sched_setaffinity(0, {cpu})
        try:
            output = run([exe, wasm, aot, str(args.sample_ms)])
        finally:
            os.sched_setaffinity(0, allowed)
        rows = [json.loads(line.removeprefix("BENCH_JSON ")) for line in output.splitlines()
                if line.startswith("BENCH_JSON ")]
        calls = [row for row in rows if row["type"] == "call"]
        loads = [row for row in rows if row["type"] == "load"]
        if len(calls) != 168 or len(loads) != 14 or not any(row["type"] == "success" for row in rows):
            raise RuntimeError(f"incomplete results: {len(calls)} call / {len(loads)} load samples")
        report["raw_samples"] = rows
        report["call_summary"] = []
        for work in (0, 1, 64, 4096):
            for mode in ("native_direct", "native_cached", "interp_cached", "interp_lookup", "aot_cached", "aot_lookup"):
                selected = [r for r in calls if r["work"] == work and r["mode"] == mode]
                if len(selected) != 7 or {r["sample"] for r in selected} != set(range(7)):
                    raise RuntimeError(f"missing/duplicate samples: {mode}/{work}")
                values = [r["elapsed_ns"] / r["iterations"] for r in selected]
                row = {"mode": mode, "work": work, "median_ns_per_call": statistics.median(values),
                    "min_ns_per_call": min(values), "max_ns_per_call": max(values)}
                report["call_summary"].append(row)
                print(f"work={work:4} {mode:14} median={row['median_ns_per_call']:12.3f} ns/call "
                      f"min={min(values):12.3f} max={max(values):12.3f}")
        report["load_summary"] = []
        for backend in ("interp", "aot"):
            values = [r["elapsed_ns"] for r in loads if r["backend"] == backend]
            row = {"backend": backend, "median_ns": statistics.median(values), "min_ns": min(values), "max_ns": max(values)}
            report["load_summary"].append(row)
            print(f"warm load/instantiate/register {backend}: {row['median_ns']/1000:.3f} us median")
        report["warnings"] = [
            f"high sample spread: {r['mode']} work={r['work']} max/min={r['max_ns_per_call']/r['min_ns_per_call']:.2f}"
            for r in report["call_summary"] if r["max_ns_per_call"] > 1.25 * r["min_ns_per_call"]]
        for warning in report["warnings"]:
            print("WARNING: " + warning, file=sys.stderr)
        report["loadavg_after"] = list(os.getloadavg())
        report["status"] = "passed"
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = str(exc)
        print(str(exc), file=sys.stderr)
        return 1
    finally:
        report["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        save()
        print(f"Report: {report_path}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
