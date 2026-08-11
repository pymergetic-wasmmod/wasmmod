#!/usr/bin/env python3
"""Populate src/pymergetic/wasmmod/rt/share/ for a release build.

Copies the *tracked* (``git ls-files``) contents of a curated set of
top-level dirs/files into the wheel's bundled source tree, so
``pip install pymergetic-wasmmod`` ships something ``wasmmod_root()``
recognizes (see ``pymergetic.wasmmod.rt``). Deliberately excludes
``third_party/`` (WAMR/LLVM submodules — huge, and rebuilding the crate
from a bundled tree isn't the point here; packing/inspecting Wasm guests
against the reference examples is) and dev-only dirs (``dev/``, ``tools/``,
tests, CI).

Share lives under the unified ``src/`` tree (no parallel ``python/``).

Run from anywhere; always resolves paths relative to this script's repo.

  python3 tools/bundle_rt_share.py            # rebuild rt/share/
  python3 tools/bundle_rt_share.py --clean     # just remove rt/share/
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

BUNDLED_TOP_LEVEL = (
    "Cargo.toml",
    "Cargo.lock",
    "src",
    "examples",
    "docs",
)


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def share_dir() -> Path:
    return repo_root() / "src" / "pymergetic" / "wasmmod" / "rt" / "share"


def tracked_files(root: Path, top_level: tuple[str, ...]) -> list[str]:
    out = subprocess.run(
        ["git", "ls-files", "--", *top_level],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    # Never nest the generated share tree into itself.
    skip_prefix = "src/pymergetic/wasmmod/rt/share/"
    return [line for line in out.splitlines() if line and not line.startswith(skip_prefix)]


def main() -> int:
    root = repo_root()
    dest = share_dir()

    if "--clean" in sys.argv[1:]:
        shutil.rmtree(dest, ignore_errors=True)
        return 0

    shutil.rmtree(dest, ignore_errors=True)
    dest.mkdir(parents=True, exist_ok=True)

    files = tracked_files(root, BUNDLED_TOP_LEVEL)
    if not files:
        print("bundle_rt_share: no tracked files found — not a git checkout?", file=sys.stderr)
        return 1

    for rel in files:
        src = root / rel
        out = dest / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, out)

    print(f"bundle_rt_share: copied {len(files)} tracked files -> {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
