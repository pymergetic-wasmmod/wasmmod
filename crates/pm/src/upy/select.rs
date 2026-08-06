//! Thin wraps for `upy` / `select` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_select_available`.
pub fn select_available() -> i32 {
    unsafe { ffi::pm_upy_select_available() }
}

/// `pm_upy_select_poll`.
pub fn select_poll() -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_select_poll() }
}
