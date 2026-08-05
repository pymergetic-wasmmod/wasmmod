#!/usr/bin/env python3
"""Shim → pymergetic.wasmmod.tools.zlib (install pymergetic-wasmmod-tools)."""
from __future__ import annotations

try:
    from pymergetic.wasmmod.tools.zlib import *  # noqa: F403
    from pymergetic.wasmmod.tools.zlib import main
except ImportError as exc:
    raise SystemExit(
        "Install pymergetic-wasmmod-tools (or editable os-sdk packages/wasmmod-tools).\n"
        f"Import error: {exc}"
    ) from exc

if __name__ == "__main__":
    raise SystemExit(main())
