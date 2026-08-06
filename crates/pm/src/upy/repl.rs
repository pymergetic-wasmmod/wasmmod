//! Thin wraps for `upy` / `repl` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};
use core::ffi::CStr;

/// `pm_upy_repl_active`.
pub fn repl_active() -> i32 {
    unsafe { ffi::pm_upy_repl_active() }
}

/// `pm_upy_repl_autocomplete`.
pub fn repl_autocomplete(src: *const core::ffi::c_char, out: *mut core::ffi::c_char, out_len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_repl_autocomplete(src, out, out_len) })
}

/// `pm_upy_repl_banner`.
pub fn repl_banner() -> &'static str {
    unsafe {
        let p = ffi::pm_upy_repl_banner();
        if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
    }
}

/// `pm_upy_repl_continue`.
pub fn repl_continue(src: *const core::ffi::c_char) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_repl_continue(src) })
}

/// `pm_upy_repl_feed_line`.
pub fn repl_feed_line(line: *const core::ffi::c_char, len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_repl_feed_line(line, len) })
}

/// `pm_upy_repl_prompt`.
pub fn repl_prompt() -> &'static str {
    unsafe {
        let p = ffi::pm_upy_repl_prompt();
        if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
    }
}

/// `pm_upy_repl_start`.
pub fn repl_start() -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_repl_start() })
}

/// `pm_upy_repl_stop`.
pub fn repl_stop() -> () {
    unsafe { ffi::pm_upy_repl_stop() }
}
