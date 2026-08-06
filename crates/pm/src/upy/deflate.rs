//! Thin wraps for `upy` / `deflate` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_deflate_available`.
pub fn deflate_available() -> i32 {
    unsafe { ffi::pm_upy_deflate_available() }
}

/// `pm_upy_deflate_decompress`.
pub fn deflate_decompress(in_: *const u8, in_len: usize, out: *mut u8, out_len: *mut usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_deflate_decompress(in_, in_len, out, out_len) })
}
