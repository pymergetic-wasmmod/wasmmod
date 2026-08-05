"""wasmmod runtime tree + path helper.

On PyPI this package ships a trimmed checkout under ``share/`` so
``WASMMOD_ROOT``-style tooling works after ``pip install pymergetic-wasmmod``.
In a git checkout, ``root()`` walks up to the real repo (``loader.c`` / crates).
"""

from __future__ import annotations

from pathlib import Path

__all__ = ["root", "require_root"]


def _looks_like(path: Path) -> bool:
    return (path / "loader.c").is_file() or (path / "crates" / "wasmmod-read").is_dir()


def root() -> Path | None:
    """Return the wasmmod source tree, or None if not found."""
    here = Path(__file__).resolve().parent
    share = here / "share"
    if _looks_like(share):
        return share
    for parent in (here, *here.parents):
        if _looks_like(parent):
            return parent
    return None


def require_root() -> Path:
    r = root()
    if r is None:
        raise RuntimeError(
            "wasmmod source tree not found. Install pymergetic-wasmmod from PyPI "
            "or set WASMMOD_ROOT to a checkout containing loader.c / crates/."
        )
    return r
