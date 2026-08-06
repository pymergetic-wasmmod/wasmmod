//! Thin wraps for `upy` / `bluetooth` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_bluetooth_available`.
pub fn bluetooth_available() -> i32 {
    unsafe { ffi::pm_upy_bluetooth_available() }
}

/// `pm_upy_bluetooth_init`.
pub fn bluetooth_init() -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_bluetooth_init() }
}
