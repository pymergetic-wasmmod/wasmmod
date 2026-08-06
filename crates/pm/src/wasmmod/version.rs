//! Thin wraps for `wasmmod` / `version` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::wasmmod::ffi::{self, *};
use core::ffi::CStr;

/// `pm_wasmmod_version`.
pub fn version() -> &'static str {
    unsafe {
        let p = ffi::pm_wasmmod_version();
        if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
    }
}
