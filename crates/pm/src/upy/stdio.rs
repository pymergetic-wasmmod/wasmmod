//! Thin wraps for `upy` / `stdio` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_stdin_rx`.
pub fn stdin_rx() -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_stdin_rx() })
}

/// `pm_upy_stdio_poll`.
pub fn stdio_poll(poll: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_stdio_poll(poll) })
}

/// `pm_upy_stdout_tx`.
pub fn stdout_tx(s: *const core::ffi::c_char, len: usize) -> () {
    unsafe { ffi::pm_upy_stdout_tx(s, len) }
}
