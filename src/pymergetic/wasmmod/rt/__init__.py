"""pymergetic.wasmmod.rt — bundled wasmmod source-tree accessor.

Lives in the unified ``src/`` tree (``src/pymergetic/wasmmod/rt/``).

``pymergetic-wasmmod`` (this distribution) is "tools + bundled source
tree in one install":

  pip install pymergetic-wasmmod
  wasmmod pack|sign|inspect|cdn|publish …

It depends on ``pymergetic-wasmmod-tools`` (the CLI) and, for release
wheels, carries a copy of the wasmmod checkout (``Cargo.toml``, ``src/``,
``examples/``, ``docs/`` — tracked files only, via ``tools/bundle_rt_share.py``)
under ``share/``. That's what lets ``pip install pymergetic-wasmmod`` give
the CLI something to pack/inspect against without a separate git checkout —
see ``pymergetic.wasmmod.tools.paths.wasmmod_root()``, which tries this as
one of its fallbacks.

Dev/editable installs and plain ``git clone`` checkouts don't have
``share/`` populated (it's build-generated, gitignored) — :func:`root`
returns ``None`` in that case, which is fine: ``wasmmod_root()`` finds the
real checkout via ``cwd`` parents first anyway.

The PyPI wheel only ships this ``rt`` package (see root ``pyproject.toml``);
``pymergetic.wasmmod`` stays a PEP 420 namespace so ``-tools`` / ``-test`` /
``-cdn`` / ``-cdn_client`` can contribute siblings. The guest-source-tree
card ``src/pymergetic/wasmmod/__pmm__.toml`` (``impl = "py"``) is a
separate FQN concern and is not what the wheel publishes.
"""

from __future__ import annotations

from pathlib import Path


def root() -> Path | None:
    """Return the bundled wasmmod checkout root, or ``None`` if absent."""
    share = Path(__file__).resolve().parent / "share"
    if (share / "Cargo.toml").is_file():
        return share
    return None
