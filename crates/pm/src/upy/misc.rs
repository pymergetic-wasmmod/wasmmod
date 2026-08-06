//! Thin wraps for `upy` / `misc` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_hw_available`.
pub fn hw_available() -> i32 {
    unsafe { ffi::pm_upy_hw_available() }
}

/// `pm_upy_ops_available`.
pub fn ops_available() -> i32 {
    unsafe { ffi::pm_upy_ops_available() }
}
