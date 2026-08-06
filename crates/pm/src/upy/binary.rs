//! Thin wraps for `upy` / `binary` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_binary_available`.
pub fn binary_available() -> i32 {
    unsafe { ffi::pm_upy_binary_available() }
}

/// `pm_upy_binary_get`.
pub fn binary_get(typecode: i32, p: *const core::ffi::c_void, out: *mut i64) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_binary_get(typecode, p, out) })
}

/// `pm_upy_binary_set`.
pub fn binary_set(typecode: i32, p: *mut core::ffi::c_void, val: i64) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_binary_set(typecode, p, val) })
}
