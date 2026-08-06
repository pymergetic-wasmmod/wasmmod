//! Thin wraps for `upy` / `native` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_dynruntime_available`.
pub fn dynruntime_available() -> i32 {
    unsafe { ffi::pm_upy_dynruntime_available() }
}

/// `pm_upy_dynruntime_load`.
pub fn dynruntime_load(mpy: *const u8, len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_dynruntime_load(mpy, len) })
}
