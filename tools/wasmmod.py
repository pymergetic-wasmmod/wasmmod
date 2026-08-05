#!/usr/bin/env python3
"""Shim → ``pymergetic-wasmmod-tools`` (``wasmmod`` on PATH after pip install).

  pip install --pre pymergetic-wasmmod   # tools + bundled source tree
  wasmmod pack|sign|inspect|cdn|publish …
"""
from __future__ import annotations

try:
    from pymergetic.wasmmod.tools.__main__ import main  # type: ignore
except ImportError as exc:
    raise SystemExit(
        "Install the host package:\n"
        "  pip install --pre pymergetic-wasmmod\n"
        "  # or editable: pip install -e ../../../../wasmmod-tools\n"
        f"Import error: {exc}"
    ) from exc

if __name__ == "__main__":
    raise SystemExit(main())
