//! Thin wraps for `upy` / `websocket` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::upy::ffi::{self, *};

/// `pm_upy_websocket_available`.
pub fn websocket_available() -> i32 {
    unsafe { ffi::pm_upy_websocket_available() }
}

/// `pm_upy_websocket_wrap`.
pub fn websocket_wrap(sock: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_websocket_wrap(sock) }
}
