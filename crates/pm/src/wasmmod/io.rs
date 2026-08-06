//! Thin wraps for `wasmmod` / `io` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::wasmmod::ffi::{self, *};

/// `pm_wasmmod_io_get`.
pub fn io_get() -> *const pm_wasmmod_io_ops_t {
    unsafe { ffi::pm_wasmmod_io_get() }
}

/// `pm_wasmmod_io_set`.
pub fn io_set(ops: *const pm_wasmmod_io_ops_t) -> () {
    unsafe { ffi::pm_wasmmod_io_set(ops) }
}

/// `pm_wasmmod_io_yield`.
pub fn io_yield() -> () {
    unsafe { ffi::pm_wasmmod_io_yield() }
}
