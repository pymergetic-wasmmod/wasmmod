# `pymergetic.wasmmod` (Python namespace)

- `rt/` — installed host entry + optional `share/` tree (wheel).
- `tools` — **symlink** to nested submodule [`dev/tools`](../../../../dev/tools)
  for local analysis/`PYTHONPATH=python`. Runtime and PyPI use the
  `pymergetic-wasmmod-tools` dependency; the symlink is not packaged.
