//! Thin wraps for `upy` / `mem` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_alloc`.
pub fn alloc(size: usize) -> *mut core::ffi::c_void {
    unsafe { ffi::pm_upy_alloc(size) }
}

/// `pm_upy_free`.
pub fn free(ptr: *mut core::ffi::c_void) -> () {
    unsafe { ffi::pm_upy_free(ptr) }
}

/// `pm_upy_gc_collect`.
pub fn gc_collect() -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_gc_collect() })
}

/// `pm_upy_gc_enabled`.
pub fn gc_enabled() -> i32 {
    unsafe { ffi::pm_upy_gc_enabled() }
}

/// `pm_upy_realloc`.
pub fn realloc(ptr: *mut core::ffi::c_void, size: usize) -> *mut core::ffi::c_void {
    unsafe { ffi::pm_upy_realloc(ptr, size) }
}

/// `pm_upy_stack_check`.
pub fn stack_check() -> () {
    unsafe { ffi::pm_upy_stack_check() }
}

/// `pm_upy_stack_ctrl_init`.
pub fn stack_ctrl_init() -> () {
    unsafe { ffi::pm_upy_stack_ctrl_init() }
}
