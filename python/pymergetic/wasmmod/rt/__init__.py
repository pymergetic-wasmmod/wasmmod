"""pymergetic.wasmmod.rt — bundled wasmmod source-tree accessor.

``pymergetic-wasmmod`` (this distribution) is "tools + bundled source
tree in one install":

  pip install pymergetic-wasmmod
  wasmmod pack|sign|inspect|cdn|publish …

It depends on ``pymergetic-wasmmod-tools`` (the CLI) and, for release
wheels, carries a copy of the wasmmod checkout (``Cargo.toml``, ``src/``,
``examples/``, ``include/``, ``docs/`` — tracked files only, via
``tools/bundle_rt_share.py``) under ``share/``. That's what lets
``pip install pymergetic-wasmmod`` give the CLI something to pack/inspect
against without a separate git checkout — see
``pymergetic.wasmmod.tools.paths.wasmmod_root()``, which tries this as one
of its fallbacks.

Dev/editable installs and plain ``git clone`` checkouts don't have
``share/`` populated (it's build-generated, gitignored) — :func:`root`
returns ``None`` in that case, which is fine: ``wasmmod_root()`` finds the
real checkout via ``cwd`` parents first anyway.

Note: ``pymergetic.wasmmod`` itself (the parent of this module) is a
*namespace* package here, deliberately — ``pymergetic-wasmmod-tools``,
``-test``, ``-cdn`` and ``-cdn_client`` all contribute children under it
from separate distributions, and a concrete ``__init__.py`` at that level
breaks their editable installs (fixed __path__, no PEP 420 merge). This is
unrelated to the guest-source-tree's own ``impl = "py"`` (not
``pep420 = true``) call for ``src/pymergetic/wasmmod/__pmm__.toml`` — that
one governs the wasmmod-pack/registry FQN tree, a different concern that
happens to share a name.
"""

from __future__ import annotations

from pathlib import Path


def root() -> Path | None:
    """Return the bundled wasmmod checkout root, or ``None`` if absent."""
    share = Path(__file__).resolve().parent / "share"
    if (share / "Cargo.toml").is_file():
        return share
    return None
