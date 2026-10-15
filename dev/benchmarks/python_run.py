#!/usr/bin/env python3
"""Run an explicitly selected MicroPython binary against the host bench fixtures.

Example (after running the host fixture builder):
  python3 dev/benchmarks/python_run.py --micropython ../../ports/unix/build-wasm/micropython \
    --source dev/benchmarks/overhead_workload.c --wasm target/overhead-bench/workload.wasm \
    --aot target/overhead-bench/workload.aot --symbol overhead_mix

This is a Linux hosted measurement, not a firmware or static-C-module benchmark.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize(samples):
    """Require complete, unique, checked observations before reporting timings."""
    summary = []
    paths = ("native_ffi", "wasmmod_interp_call", "wasmmod_aot_call")
    if len(samples) != len(paths) * 4 * 7:
        raise ValueError("incomplete measurements")
    for work in (0, 1, 64, 4096):
        for path in paths:
            rows = [row for row in samples if row["path"] == path and row["work"] == work]
            if len(rows) != 7 or {row["sample"] for row in rows} != set(range(7)):
                raise ValueError(f"missing/duplicate samples: {path}/{work}")
            if len({row["checksum"] for row in rows}) != 1 or len({row["calls"] for row in rows}) != 1:
                raise ValueError(f"inconsistent checksums/counts: {path}/{work}")
            values = []
            for row in rows:
                if row["calls"] <= 0 or row["elapsed_us"] <= 0:
                    raise ValueError("nonpositive measurement")
                # Derive from raw integer data rather than MicroPython float formatting.
                values.append(row["elapsed_us"] * 1000.0 / row["calls"])
            summary.append({"work": work, "path": path,
                            "median_ns_per_call": statistics.median(values),
                            "min_ns_per_call": min(values), "max_ns_per_call": max(values)})
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--micropython", required=True, type=Path)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--wasm", required=True, type=Path)
    parser.add_argument("--aot", required=True, type=Path)
    parser.add_argument("--symbol", required=True, choices=["overhead_mix"],
                        help="Fixed workload: validated against an independent Python oracle")
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--build-command", help="Record the command actually used to build this MicroPython binary")
    parser.add_argument("--cpu", type=int, help="Default: lowest CPU in current allowed affinity")
    parser.add_argument("--output", type=Path, default=ROOT / "target/overhead-bench/python-report.json")
    args = parser.parse_args()
    allowed = os.sched_getaffinity(0)
    cpu = min(allowed) if args.cpu is None else args.cpu
    if cpu not in allowed:
        parser.error("requested CPU is outside the allowed affinity")
    os.sched_setaffinity(0, {cpu})
    args.output = args.output.resolve()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Never let a failed rerun masquerade as a previous successful report.
    args.output.write_text(json.dumps({"status": "incomplete", "started_ns": time.time_ns()}) + "\n")
    shared = args.output.parent / "python-native.so"
    command = [args.cc, "-O2", "-fno-lto", "-march=native", "-fPIC", "-shared", str(args.source.resolve()), "-o", str(shared)]
    subprocess.run(command, check=True)
    invocation = [str(args.micropython.resolve()), str(Path(__file__).with_name("python_boundary.py")),
                  str(shared), str(args.wasm.resolve()), str(args.aot.resolve()), args.symbol]
    result = subprocess.run(invocation, text=True, capture_output=True, timeout=180)
    log = args.output.with_suffix(".log")
    log.write_text(result.stdout + "\nSTDERR:\n" + result.stderr)
    result.check_returncode()
    rows = [json.loads(line[len("PYBENCH "):]) for line in result.stdout.splitlines() if line.startswith("PYBENCH ")]
    samples = [row for row in rows if row["kind"] == "sample"]
    if len(samples) != 3 * 4 * 7:
        raise RuntimeError(f"incomplete measurements: {len(samples)}, see {log}")
    summary = summarize(samples)
    for row in summary:
        print(json.dumps(row))
    metadata = {"utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "platform": platform.platform(),
                "cpu_affinity": sorted(os.sched_getaffinity(0)), "commands": [command, invocation],
                "cpu_info": subprocess.check_output(["lscpu"], text=True),
                "wasmmod_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "wasmmod_status": subprocess.check_output(["git", "status", "--short"], cwd=ROOT, text=True),
                "micropython_build_command": args.build_command,
                "micropython_checkout_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT.parents[1], text=True).strip(),
                "wamr_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT / "third_party/wamr", text=True).strip(),
                "compiler": subprocess.check_output([args.cc, "--version"], text=True),
                "artifacts": {str(p): {"sha256": digest(p), "bytes": p.stat().st_size}
                              for p in (args.micropython.resolve(), args.source.resolve(), args.wasm.resolve(), args.aot.resolve(), shared)},
                "scope": "Existing MicroPython binary; compiled provenance is not inferred from the current source checkout. Native reference uses libffi, not a static C module. No firmware, host-import callback, bulk-buffer or memory-footprint measurement."}
    warnings = [f"high sample spread: {row['path']} work={row['work']}"
                for row in summary if row["max_ns_per_call"] > 1.25 * row["min_ns_per_call"]]
    warnings += [f"short sample: {row['path']} work={row['work']} sample={row['sample']}"
                 for row in samples if row["elapsed_us"] < 10000]
    for warning in warnings:
        print("WARNING: " + warning)
    args.output.write_text(json.dumps({"status": "complete", "metadata": metadata, "rows": rows,
                                      "summary": summary, "warnings": warnings}, indent=2) + "\n")
    print(f"Report: {args.output}")


if __name__ == "__main__":
    main()
