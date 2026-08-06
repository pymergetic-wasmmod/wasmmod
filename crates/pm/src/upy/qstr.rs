//! Thin wraps for `upy` / `qstr` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};
use core::ffi::CStr;

/// `pm_upy_qstr_from_str`.
pub fn qstr_from_str(s: *const core::ffi::c_char) -> u32 {
    unsafe { ffi::pm_upy_qstr_from_str(s) }
}

/// `pm_upy_qstr_len`.
pub fn qstr_len(q: u32) -> usize {
    unsafe { ffi::pm_upy_qstr_len(q) }
}

/// `pm_upy_qstr_str`.
pub fn qstr_str(q: u32) -> &'static str {
    unsafe {
        let p = ffi::pm_upy_qstr_str(q);
        if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
    }
}
