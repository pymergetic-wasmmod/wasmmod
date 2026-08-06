//! Thin wraps for `upy` / `ssl` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_ssl_available`.
pub fn ssl_available() -> i32 {
    unsafe { ffi::pm_upy_ssl_available() }
}

/// `pm_upy_ssl_wrap_socket`.
pub fn ssl_wrap_socket(sock: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_ssl_wrap_socket(sock) }
}
