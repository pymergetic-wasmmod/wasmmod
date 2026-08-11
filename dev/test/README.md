# wasmmod-test

Independent consumer of [wasmmod](https://github.com/pymergetic-wasmmod/wasmmod): build
Wasm/ELF packs outside the runtime tree and publish to
[wasmmod-cdn](https://github.com/pymergetic-wasmmod/wasmmod-cdn).

PyPI: **`pymergetic-wasmmod-test`** · import: **`pymergetic.wasmmod.test`**

| Component | Role | Repo |
|-----------|------|------|
| wasmmod | Runtime + pack tree (`pymergetic-wasmmod`) | [pymergetic-wasmmod/wasmmod](https://github.com/pymergetic-wasmmod/wasmmod) · `main` |
| wasmmod-tools | Host CLI (`pymergetic-wasmmod-tools`) | [pymergetic-wasmmod/wasmmod-tools](https://github.com/pymergetic-wasmmod/wasmmod-tools) · `main` |
| **wasmmod-test** | **This repo** — external consumer sample | [pymergetic-wasmmod/wasmmod-test](https://github.com/pymergetic-wasmmod/wasmmod-test) · `main` |
| wasmmod-cdn | CDN server + client | [pymergetic-wasmmod/wasmmod-cdn](https://github.com/pymergetic-wasmmod/wasmmod-cdn) · `main` |
| metalpython | Metal product µPy — rebuilt on the wasmmod engine | [pymergetic/metalpython](https://github.com/pymergetic/metalpython) · `main` |

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

1. Add repo secret **`WASMMOD_CDN_TOKEN`** (wasmmod-cdn API token).
2. Actions → **publish-cdn** → set `cdn_url`, uncheck `dry_run` to upload.
3. Default path reuses prebuilt `hello.wasm` from a `pymergetic/wasmmod` checkout
   (`WASMMOD_ROOT`); set `from_wasmmod_example=false` to pack `examples/hello`
   locally (needs a Wasm / mpy-cross toolchain).
