//! pymergetic.wasmmod — regular package (see `__pmm__.toml`: `impl =
//! "py"`, a real `__init__.py` — not `pep420`, see SOURCETREE.md "pep420
//! test"). Its own body is Python, but Rust still needs a declaration
//! here for every child Rust code can reach through (`registry` today;
//! `pack`/`thunk`/etc. join as they gain real impls) — that need is
//! independent of what this node's own `impl` is.
#[path = "wasmmod/registry.rs"]
pub mod registry;

#[path = "wasmmod/loader.rs"]
pub mod loader;
