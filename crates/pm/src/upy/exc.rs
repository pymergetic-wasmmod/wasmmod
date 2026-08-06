//! Thin wraps for `upy` / `exc` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_raise_OSError`.
pub fn raise_OSError(errno_val: i32) -> () {
    unsafe { ffi::pm_upy_raise_OSError(errno_val) }
}

/// `pm_upy_raise_feature`.
pub fn raise_feature(api_name: *const core::ffi::c_char) -> () {
    unsafe { ffi::pm_upy_raise_feature(api_name) }
}

/// `pm_upy_raise_msg`.
pub fn raise_msg(type_name: *const core::ffi::c_char, msg: *const core::ffi::c_char) -> () {
    unsafe { ffi::pm_upy_raise_msg(type_name, msg) }
}
