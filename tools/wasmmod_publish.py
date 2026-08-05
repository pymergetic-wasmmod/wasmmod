#!/usr/bin/env python3
"""Shim → pymergetic.wasmmod.tools.publish (install pymergetic-wasmmod-tools)."""
from __future__ import annotations

try:
    from pymergetic.wasmmod.tools.publish import *  # noqa: F403
    from pymergetic.wasmmod.tools.publish import main
except ImportError as exc:
    raise SystemExit(
        "Install pymergetic-wasmmod-tools (or editable os-sdk packages/wasmmod-tools).\n"
        f"Import error: {exc}"
    ) from exc

if __name__ == "__main__":
    raise SystemExit(main())
