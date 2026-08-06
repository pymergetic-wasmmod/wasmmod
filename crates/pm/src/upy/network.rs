//! Thin wraps for `upy` / `network` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};
use core::ffi::CStr;

/// `pm_upy_network_active`.
pub fn network_active(nic: pm_upy_obj_t, on: i32) -> i32 {
    unsafe { ffi::pm_upy_network_active(nic, on) }
}

/// `pm_upy_network_available`.
pub fn network_available() -> i32 {
    unsafe { ffi::pm_upy_network_available() }
}

/// `pm_upy_network_connect`.
pub fn network_connect(nic: pm_upy_obj_t, ssid: *const core::ffi::c_char, key: *const core::ffi::c_char) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_network_connect(nic, ssid, key) })
}

/// `pm_upy_network_hostname`.
pub fn network_hostname() -> &'static str {
    unsafe {
        let p = ffi::pm_upy_network_hostname();
        if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
    }
}

/// `pm_upy_network_ifconfig`.
pub fn network_ifconfig(nic: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_network_ifconfig(nic) }
}

/// `pm_upy_network_status`.
pub fn network_status(nic: pm_upy_obj_t) -> i32 {
    unsafe { ffi::pm_upy_network_status(nic) }
}
