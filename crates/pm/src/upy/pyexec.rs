//! Thin wraps for `upy` / `pyexec` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_pyexec_file`.
pub fn pyexec_file(path: *const core::ffi::c_char) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_pyexec_file(path) })
}

/// `pm_upy_pyexec_vstr`.
pub fn pyexec_vstr(src: *const core::ffi::c_char, len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_pyexec_vstr(src, len) })
}
