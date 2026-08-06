//! Thin wraps for `upy` / `print` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_printf`.
pub fn printf(fmt: *const core::ffi::c_char) -> () {
    unsafe { ffi::pm_upy_printf(fmt) }
}
