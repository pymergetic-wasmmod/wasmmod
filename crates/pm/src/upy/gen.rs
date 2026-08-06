//! Thin wraps for `upy` / `gen` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_gen_available`.
pub fn gen_available() -> i32 {
    unsafe { ffi::pm_upy_gen_available() }
}
