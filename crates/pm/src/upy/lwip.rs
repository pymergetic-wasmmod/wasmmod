//! Thin wraps for `upy` / `lwip` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_lwip_available`.
pub fn lwip_available() -> i32 {
    unsafe { ffi::pm_upy_lwip_available() }
}

/// `pm_upy_lwip_gethostbyname`.
pub fn lwip_gethostbyname(host: *const core::ffi::c_char, ip: *mut u8) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_lwip_gethostbyname(host, ip) })
}

/// `pm_upy_lwip_init`.
pub fn lwip_init() -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_lwip_init() })
}

/// `pm_upy_lwip_poll`.
pub fn lwip_poll(ticks_ms: u32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_lwip_poll(ticks_ms) })
}
