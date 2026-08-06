//! Thin wraps for `upy` / `compile` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_compile`.
pub fn compile(src: *const core::ffi::c_char, filename: *const core::ffi::c_char, kind: i32) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_compile(src, filename, kind) }
}

/// `pm_upy_compile_available`.
pub fn compile_available() -> i32 {
    unsafe { ffi::pm_upy_compile_available() }
}

/// `pm_upy_compile_to_raw_code`.
pub fn compile_to_raw_code(src: *const core::ffi::c_char, raw_out: *mut *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_compile_to_raw_code(src, raw_out) })
}
