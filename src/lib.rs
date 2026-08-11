//! pymergetic-wasmmod — crate root.
//!
//! This crate's root doubles as the Rust entry point for the whole
//! `pymergetic` namespace (see docs/SOURCETREE.md "Crate placement" /
//! "Rust and PEP 420"). Rust has no PEP-420 equivalent — no dynamic,
//! multi-distribution namespace merge at compile time, only single-owner
//! `mod`/`use` declarations — so unlike `pymergetic/__pmm__.toml`'s
//! `pep420 = true` (which needs zero Rust or Python files to exist),
//! this level still needs exactly one declaration file. This *is* that
//! file; there's no separate `pymergetic.rs` beside it, since crate root
//! and "the pymergetic namespace's Rust half" are the same thing here.
//!
//! Depend on this crate under the local name `pymergetic`
//! (`pymergetic = { package = "pymergetic-wasmmod", path = "..." }` in a
//! consumer's `Cargo.toml`) so `use pymergetic::util::mem;` reads
//! exactly like the dotted path it mirrors — no
//! `pymergetic_wasmmod::pymergetic::` stutter.
#![cfg_attr(not(test), no_std)]

// Unlike `core`, `alloc` isn't implicitly linked/path-resolvable even in
// a 2018+-edition crate — it needs one explicit `extern crate alloc;`
// declaration to exist at all as a usable path. Declaring it here, at
// crate root, rather than per-module, is what makes a bare `alloc::`
// path resolve from *any* module in the crate (registry, lz4, ...); a
// leaf module's own `extern crate alloc;` would only be visible inside
// that module. This isn't `no_std`-only, either: `alloc` ships as part
// of every Rust toolchain regardless of `std`, so this line is a no-op
// either way — that's deliberate, it's what lets registry/lz4 use plain
// `alloc::` paths uniformly instead of switching between `alloc::` and
// `std::` per build mode.
extern crate alloc;

#[path = "pymergetic/util.rs"]
pub mod util;

#[path = "pymergetic/wasmmod.rs"]
pub mod wasmmod;
