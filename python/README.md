# pymergetic-wasmmod

Python namespace root for [wasmmod](https://github.com/pymergetic-wasmmod/wasmmod).

The wasmmod loader/registry is native code — a Rust crate (`Cargo.toml` at
the repo root) embedded into MicroPython/CPython hosts at build time, not
a Python extension shipped from here. This package exists to reserve the
`pymergetic-wasmmod` PyPI name and anchor the `pymergetic.wasmmod`
namespace for the packages that publish real content under it:

| Package | Import |
|---|---|
| `pymergetic-wasmmod-tools` | `pymergetic.wasmmod.tools` |
| `pymergetic-wasmmod-test` | `pymergetic.wasmmod.test` |
| `pymergetic-wasmmod-cdn` | `pymergetic.wasmmod.cdn` |
| `pymergetic-wasmmod-cdn-client` | `pymergetic.wasmmod.cdn_client` |

```sh
pip install pymergetic-wasmmod
python -c "import pymergetic.wasmmod"
```

PEP 420: `pymergetic` is a namespace; `wasmmod` is a real package (this one).

## Trusted Publishing

| Field | Value |
|-------|--------|
| PyPI Project Name | `pymergetic-wasmmod` |
| Owner | `pymergetic-wasmmod` |
| Repository | `wasmmod` |
| Workflow name | `publish-pypi.yml` |
| Environment | *(empty / any)* |
