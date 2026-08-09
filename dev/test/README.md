# wasmmod-test

Independent consumer of [wasmmod](https://github.com/pymergetic/wasmmod): build
Wasm/ELF packs outside the runtime tree and publish to
[metal-cdn](https://github.com/pymergetic/metal-cdn).

PyPI: **`pymergetic-wasmmod-test`** · import: **`pymergetic.wasmmod.test`**

| Component | Role | Repo |
|-----------|------|------|
| wasmmod | Runtime + pack tree (`pymergetic-wasmmod`) | [pymergetic/wasmmod](https://github.com/pymergetic/wasmmod) · `main` |
| wasmmod-tools | Host CLI (`pymergetic-wasmmod-tools`) | [pymergetic/wasmmod-tools](https://github.com/pymergetic/wasmmod-tools) · `main` |
| **wasmmod-test** | **This repo** — external consumer sample | [pymergetic/wasmmod-test](https://github.com/pymergetic/wasmmod-test) · `main` |
| metal-cdn | CDN server + client | [pymergetic/metal-cdn](https://github.com/pymergetic/metal-cdn) · `main` |
| metalpython `wasmmod` | Clean upy host + submodule (upstream-shaped) | [metalpython/tree/wasmmod](https://github.com/pymergetic/metalpython/tree/wasmmod) |
| metalpython `master` | Metal product µPy; **base = `wasmmod` tip** | [metalpython/tree/master](https://github.com/pymergetic/metalpython/tree/master) |

```sh
pip install pymergetic-wasmmod-test
python -c "import pymergetic.wasmmod.test"
```

PEP 420: `pymergetic` / `wasmmod` are namespaces; `test` is the leaf.

## Trusted Publishing

| Field | Value |
|-------|--------|
| PyPI Project Name | `pymergetic-wasmmod-test` |
| Owner | `pymergetic` |
| Repository | `wasmmod-test` |
| Workflow name | `publish-pypi.yml` |
| Environment | *(empty / any)* |


## CDN publish (ultima consumer)

Workflow: `.github/workflows/publish-cdn.yml` (workflow_dispatch).

1. Add repo secret **`METAL_CDN_TOKEN`** (metal-cdn API token).
2. Actions → **publish-cdn** → set `cdn_url`, uncheck `dry_run` to upload.
3. Default path reuses prebuilt `hello.wasm` from a `pymergetic/wasmmod` checkout
   (`WASMMOD_ROOT`); set `from_wasmmod_example=false` to pack `examples/hello`
   locally (needs a Wasm / mpy-cross toolchain).
