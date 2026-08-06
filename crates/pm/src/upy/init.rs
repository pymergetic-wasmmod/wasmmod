//! Thin wraps for `upy` / `init` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_deinit`.
pub fn deinit() -> () {
    unsafe { ffi::pm_upy_deinit() }
}

/// `pm_upy_init`.
pub fn init(heap_start: *mut core::ffi::c_void, heap_len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_init(heap_start, heap_len) })
}

/// `pm_upy_ready`.
pub fn ready() -> i32 {
    unsafe { ffi::pm_upy_ready() }
}
