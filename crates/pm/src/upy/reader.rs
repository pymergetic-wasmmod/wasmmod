//! Thin wraps for `upy` / `reader` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_reader_available`.
pub fn reader_available() -> i32 {
    unsafe { ffi::pm_upy_reader_available() }
}

/// `pm_upy_reader_new_file`.
pub fn reader_new_file(path: *const core::ffi::c_char) -> *mut core::ffi::c_void {
    unsafe { ffi::pm_upy_reader_new_file(path) }
}

/// `pm_upy_reader_new_mem`.
pub fn reader_new_mem(data: *const u8, len: usize) -> *mut core::ffi::c_void {
    unsafe { ffi::pm_upy_reader_new_mem(data, len) }
}
