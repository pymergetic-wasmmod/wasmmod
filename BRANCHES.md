# Host branches (MetalPython ↔ MicroPython)

> **Experimental integration.** The `wasmmod` host branch is the clean PR track for
> an alpha / default-off submodule. Do not treat the pin or glue as a stable
> upstream contract until MicroPython (or a non-alpha wasmmod release) says so.

[`pymergetic/metalpython`](https://github.com/pymergetic/metalpython) is a MicroPython
fork that vendors this repo at `extmod/wasmmod`. We keep **two host branches** there
because we cannot maintain two separate upy forks:

```text
upstream/master   (micropython/micropython)
        │
        ▼
   metalpython wasmmod   ← clean wasmmod→upy integration (PR candidate)
        │                   submodule bumps + thin host glue only
        │                   no MetalPython product extras
        ▼
   metalpython master    ← MetalPython product
                            = wasmmod tip
                            + Rust rewrite / other product work
```

| Host branch | Role | Drop when |
|-------------|------|-----------|
| `wasmmod` | Upy-shaped host with this package wired in; PR fodder for upstream | Upstream accepts the integration |
| `master` | Day-to-day MetalPython; **must descend from `wasmmod`** | — (product default) |

## Workflow

1. Land changes in **this** repo (`pymergetic/wasmmod` `main`) first.
2. Bump the submodule / host glue on metalpython **`wasmmod`**.
3. Fast-forward or rebase metalpython **`master`** onto that tip, then add product-only commits.
4. Do **not** dual-commit the same submodule bump on both host branches in parallel.

## Remotes (metalpython)

- `origin` → `pymergetic/metalpython`
- `upstream` → `micropython/micropython`
- Submodule `extmod/wasmmod` → `pymergetic/wasmmod` (`main`)
