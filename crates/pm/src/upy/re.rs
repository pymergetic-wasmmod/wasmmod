//! Thin wraps for `upy` / `re` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_re_available`.
pub fn re_available() -> i32 {
    unsafe { ffi::pm_upy_re_available() }
}

/// `pm_upy_re_compile`.
pub fn re_compile(pat: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_re_compile(pat) }
}

/// `pm_upy_re_match`.
pub fn re_match(regex: pm_upy_obj_t, s: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_re_match(regex, s) }
}
