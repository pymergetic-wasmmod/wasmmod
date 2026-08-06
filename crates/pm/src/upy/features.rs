//! Thin wraps for `upy` / `features` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};
use core::ffi::CStr;

/// `pm_upy_features`.
pub fn features() -> u32 {
    unsafe { ffi::pm_upy_features() }
}

/// `pm_upy_has`.
pub fn has(feat: pm_upy_feat_t) -> bool {
    unsafe { ffi::pm_upy_has(feat) }
}

/// `pm_upy_version`.
pub fn version() -> &'static str {
    unsafe {
        let p = ffi::pm_upy_version();
        if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
    }
}
