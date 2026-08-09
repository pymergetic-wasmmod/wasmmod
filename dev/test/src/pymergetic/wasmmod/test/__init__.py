"""Independent wasmmod consumer: example packs + CDN publish exercises.

Import: ``pymergetic.wasmmod.test``
PyPI: ``pymergetic-wasmmod-test``

Example pack tree: ``examples/hello/`` (build with ``wasmmod pack`` once
``WASMMOD_ROOT`` and a Wasm toolchain are available).
"""

from __future__ import annotations

from pathlib import Path

__all__ = ["example_hello_dir"]


def example_hello_dir() -> Path:
    """Return the path to the bundled ``examples/hello`` pack (repo checkout)."""
    here = Path(__file__).resolve()
    for parent in here.parents:
        cand = parent / "examples" / "hello"
        if (cand / "pack.toml").is_file():
            return cand
    raise FileNotFoundError("examples/hello not found (install from git checkout)")
