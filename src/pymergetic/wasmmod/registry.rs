//! pymergetic.wasmmod.registry — thin reexport. The real impl lives in
//! registry/__impl__.rs (tucked away with this module's faces); the
//! shared ABI shapes live in registry/__types__.rs (the Rust twin of
//! `__types__.h`), path-included by the impl itself so both cargo and
//! the in-kernel rsx compile see the one definition. This file only
//! exists so `pymergetic::wasmmod::registry` resolves at the path
//! "path == module" expects.
#[path = "registry/__impl__.rs"]
mod r#impl;
pub use r#impl::*;
