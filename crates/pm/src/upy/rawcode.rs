//! Thin wraps for `upy` / `rawcode` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_find_frozen`.
pub fn find_frozen(name: *const core::ffi::c_char, out: *mut *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_find_frozen(name, out) })
}

/// `pm_upy_raw_code_load_file`.
pub fn raw_code_load_file(path: *const core::ffi::c_char, raw_out: *mut *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_raw_code_load_file(path, raw_out) })
}

/// `pm_upy_raw_code_load_mem`.
pub fn raw_code_load_mem(data: *const u8, len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_raw_code_load_mem(data, len) })
}

/// `pm_upy_raw_code_save`.
pub fn raw_code_save() -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_raw_code_save() })
}
