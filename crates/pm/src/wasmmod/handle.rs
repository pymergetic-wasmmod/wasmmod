//! Thin wraps for `wasmmod` / `handle` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::wasmmod::ffi::{self, *};

/// `pm_wasmmod_handle_clear_all`.
pub fn handle_clear_all() -> () {
    unsafe { ffi::pm_wasmmod_handle_clear_all() }
}

/// `pm_wasmmod_handle_free`.
pub fn handle_free(handle: i32) -> bool {
    unsafe { ffi::pm_wasmmod_handle_free(handle) }
}

/// `pm_wasmmod_handle_register_ptr`.
pub fn handle_register_ptr(obj: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_wasmmod_handle_register_ptr(obj) })
}

/// `pm_wasmmod_handle_resolve_ptr`.
pub fn handle_resolve_ptr(handle: i32) -> *mut core::ffi::c_void {
    unsafe { ffi::pm_wasmmod_handle_resolve_ptr(handle) }
}
