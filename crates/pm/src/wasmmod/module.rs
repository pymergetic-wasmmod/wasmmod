//! Thin wraps for `wasmmod` / `module` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::wasmmod::ffi::{self, *};
use core::ffi::CStr;

/// `pm_wasmmod_module_install`.
pub fn module_install(full_name: *const core::ffi::c_char) -> bool {
    unsafe { ffi::pm_wasmmod_module_install(full_name) }
}

/// `pm_wasmmod_module_installed`.
pub fn module_installed() -> bool {
    unsafe { ffi::pm_wasmmod_module_installed() }
}

/// `pm_wasmmod_module_name`.
pub fn module_name() -> &'static str {
    unsafe {
        let p = ffi::pm_wasmmod_module_name();
        if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
    }
}
