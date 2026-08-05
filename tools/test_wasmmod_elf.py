"""Shim test — run from packages/wasmmod-tools/tests/test_wasmmod_elf.py with WASMMOD_ROOT set."""
from __future__ import annotations

import runpy
import sys
from pathlib import Path

_pkg_tests = Path(__file__).resolve().parents[4] / "wasmmod-tools" / "tests" / "test_wasmmod_elf.py"
# metalpython/extmod/wasmmod/tools → parents[4]=packages
if not _pkg_tests.is_file():
    _pkg_tests = Path.home() / "Devel/os-sdk/packages/wasmmod-tools/tests/test_wasmmod_elf.py"
if _pkg_tests.is_file():
    sys.path.insert(0, str(_pkg_tests.parent.parent / "src"))
    runpy.run_path(str(_pkg_tests), run_name="__main__")
else:
    raise SystemExit(f"missing package tests at {_pkg_tests}")
