"""pymergetic.wasmmod — Python namespace root for the wasmmod project.

Reserves the ``pymergetic-wasmmod`` PyPI name. The wasmmod loader/registry
itself is native code — a Rust crate (``Cargo.toml`` at the repo root)
embedded into MicroPython/CPython hosts at build time, not a Python
extension shipped here.

This package currently exists to anchor the ``pymergetic.wasmmod``
namespace on PyPI for the sibling packages that publish real content under
it:

- ``pymergetic-wasmmod-tools`` -> ``pymergetic.wasmmod.tools``
- ``pymergetic-wasmmod-test`` -> ``pymergetic.wasmmod.test``
- ``pymergetic-wasmmod-cdn`` -> ``pymergetic.wasmmod.cdn``
- ``pymergetic-wasmmod-cdn-client`` -> ``pymergetic.wasmmod.cdn_client``

A future ``pymergetic.wasmmod.rt`` (bundled runtime bindings) may land here
once there's a real Python-facing surface to ship.
"""

from __future__ import annotations
