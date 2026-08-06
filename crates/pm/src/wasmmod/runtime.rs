//! Thin wraps for `wasmmod` / `runtime` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::wasmmod::ffi::{self, *};

/// `pm_wasmmod_runtime_deinit`.
pub fn runtime_deinit() -> () {
    unsafe { ffi::pm_wasmmod_runtime_deinit() }
}

/// `pm_wasmmod_runtime_init`.
pub fn runtime_init() -> bool {
    unsafe { ffi::pm_wasmmod_runtime_init() }
}
