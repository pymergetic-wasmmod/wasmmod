//! Thin wraps for `upy` / `uctypes` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_uctypes_available`.
pub fn uctypes_available() -> i32 {
    unsafe { ffi::pm_upy_uctypes_available() }
}

/// `pm_upy_uctypes_struct`.
pub fn uctypes_struct(addr: u32, desc: pm_upy_obj_t, flags: i32) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_uctypes_struct(addr, desc, flags) }
}
