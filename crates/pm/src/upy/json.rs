//! Thin wraps for `upy` / `json` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_json_available`.
pub fn json_available() -> i32 {
    unsafe { ffi::pm_upy_json_available() }
}

/// `pm_upy_json_dumps`.
pub fn json_dumps(o: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_json_dumps(o) }
}

/// `pm_upy_json_loads`.
pub fn json_loads(s: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_json_loads(s) }
}
