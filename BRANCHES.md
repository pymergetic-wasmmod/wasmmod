# Host branches (MetalPython ↔ MicroPython)

> **Experimental integration.** The `wasmmod` host branch is the clean PR track for
> an alpha / default-off submodule. Do not treat the pin or glue as a stable
> upstream contract until MicroPython (or a non-alpha wasmmod release) says so.

[`pymergetic/metalpython`](https://github.com/pymergetic/metalpython) is a MicroPython
fork that vendors this repo at `extmod/wasmmod`. We keep **two host branches** there
because we cannot maintain two separate upy forks:

```text
upstream/master   (micropython/micropython)     ← clean upstream µPy
        │
        ▼
   metalpython wasmmod   ← clean wasmmod→upy integration (PR candidate)
        │                   submodule bumps + thin host glue only
        │                   built/tested against upstream-shaped tree
        │                   no MetalPython product extras
        ▼
   metalpython master    ← MetalPython product (Metal-specific µPy)
                            MUST have metalpython wasmmod as ancestor
                            = wasmmod tip + Rust rewrite / other product work
```

| Host branch | Role | Drop when |
|-------------|------|-----------|
| [`wasmmod`](https://github.com/pymergetic/metalpython/tree/wasmmod) | Upy-shaped host with this package wired in; PR fodder for upstream | Upstream accepts the integration |
| [`master`](https://github.com/pymergetic/metalpython/tree/master) | Day-to-day MetalPython; **must descend from `wasmmod`** | — (product default) |

**Name clash:** this package’s GitHub default branch is `main`. That is **not** the
metalpython host branch `wasmmod`. Constellation tables spell both.

## Health check (metalpython checkout)

```sh
# wasmmod must be an ancestor of master (base-of-product invariant):
git merge-base --is-ancestor wasmmod master && echo OK

# Prefer zero submodule bumps only on master — land bumps on wasmmod first:
git log --oneline wasmmod..master --grep='Bump wasmmod'
```

If the second command prints commits, `master` raced ahead: FF/rebase those bumps
onto `wasmmod`, then re-merge so product again sits on the clean tip.

## Workflow

1. Land changes in **this** repo (`pymergetic/wasmmod` `main`) first.
2. Bump the submodule / host glue on metalpython **`wasmmod`**.
3. Fast-forward or rebase metalpython **`master`** onto that tip, then add product-only commits.
4. Do **not** dual-commit the same submodule bump on both host branches in parallel.

## Remotes (metalpython)

- `origin` → `pymergetic/metalpython`
- `upstream` → `micropython/micropython`
- Submodule `extmod/wasmmod` → `pymergetic/wasmmod` (`main`)
