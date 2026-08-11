# pymergetic-wasmmod

Tools + bundled source tree in one install, for [wasmmod](https://github.com/pymergetic-wasmmod/wasmmod).

```sh
pip install pymergetic-wasmmod
wasmmod pack|sign|inspect|cdn|publish …
```

Depends on `pymergetic-wasmmod-tools` (the `wasmmod` CLI) and bundles a
copy of the wasmmod checkout under `pymergetic/wasmmod/rt/share/` (tracked
files only — `Cargo.toml`, `src/`, `examples/`, `include/`, `docs/`; see
`tools/bundle_rt_share.py`), so the CLI has a reference source tree to
pack/inspect against without a separate git checkout.

Prefer a real checkout when you have one — set `WASMMOD_ROOT`, or just run
from inside one; `wasmmod_root()` prefers a live `cwd`-parents checkout
over the bundled copy, which can lag.

The wasmmod loader/registry itself is native code — a Rust crate
(`Cargo.toml` at the repo root) embedded into MicroPython/CPython hosts at
build time, not a Python extension shipped from here. This package also
anchors the `pymergetic.wasmmod` namespace on PyPI for the packages that
publish real content under it:

| Package | Import |
|---|---|
| `pymergetic-wasmmod-tools` | `pymergetic.wasmmod.tools` |
| `pymergetic-wasmmod-test` | `pymergetic.wasmmod.test` |
| `pymergetic-wasmmod-cdn` | `pymergetic.wasmmod.cdn` |
| `pymergetic-wasmmod-cdn-client` | `pymergetic.wasmmod.cdn_client` |

PEP 420: `pymergetic` is a namespace; `wasmmod` is a real package (this one).

## Trusted Publishing

| Field | Value |
|-------|--------|
| PyPI Project Name | `pymergetic-wasmmod` |
| Owner | `pymergetic-wasmmod` |
| Repository | `wasmmod` |
| Workflow name | `publish-pypi.yml` |
| Environment | *(empty / any)* |
