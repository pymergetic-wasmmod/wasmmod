//! Thin wraps for `upy` / `step` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_loop_feed`.
pub fn loop_feed(ptr: *const u8, len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_loop_feed(ptr, len) })
}

/// `pm_upy_loop_reset`.
pub fn loop_reset() -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_loop_reset() })
}

/// `pm_upy_loop_step`.
pub fn loop_step() -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_loop_step() })
}
