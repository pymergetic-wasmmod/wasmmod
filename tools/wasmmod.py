#!/usr/bin/env python3
"""Shim → in-tree ``pymergetic-wasmmod-tools`` (or pip install).

Prefers ``extmod/wasmmod/dev/tools/src`` next to this repo so ``make test``
works without pip on PEP-668 hosts.
"""
from __future__ import annotations

import sys
from pathlib import Path

_tools_src = Path(__file__).resolve().parents[1] / "dev" / "tools" / "src"
if _tools_src.is_dir():
    p = str(_tools_src)
    if p not in sys.path:
        sys.path.insert(0, p)

try:
    from pymergetic.wasmmod.tools.__main__ import main  # type: ignore
except ImportError as exc:
    raise SystemExit(
        "Install the host package:\n"
        "  pip install --pre pymergetic-wasmmod\n"
        "  # or editable: pip install -e extmod/wasmmod/dev/tools\n"
        "  # or: PYTHONPATH=extmod/wasmmod/dev/tools/src\n"
        f"Import error: {exc}"
    ) from exc

if __name__ == "__main__":
    raise SystemExit(main())
