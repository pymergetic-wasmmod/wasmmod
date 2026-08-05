#!/usr/bin/env python3
"""Shim: host CLI moved to ``pymergetic-wasmmod-tools``.

  pip install pymergetic-wasmmod-tools
  wasmmod <command> …
"""
from __future__ import annotations

import sys

try:
    from pymergetic.wasmmod.tools.__main__ import main
except ImportError as exc:
    raise SystemExit(
        "wasmmod tools moved to PyPI package pymergetic-wasmmod-tools.\n"
        "  pip install -e ../../../../wasmmod-tools   # os-sdk layout\n"
        "  # or: pip install pymergetic-wasmmod-tools\n"
        f"Import error: {exc}"
    ) from exc

if __name__ == "__main__":
    raise SystemExit(main())
