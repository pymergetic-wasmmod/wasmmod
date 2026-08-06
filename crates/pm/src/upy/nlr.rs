//! Thin wraps for `upy` / `nlr` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_nlr_available`.
pub fn nlr_available() -> i32 {
    unsafe { ffi::pm_upy_nlr_available() }
}
