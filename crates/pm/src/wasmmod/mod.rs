//! Wasmmod host surface (`pm_wasmmod_*`).
//!
//! Raw C names live in [`ffi`]; prefer thin wraps in sibling modules.

use std::ffi::CStr;
use std::os::raw::c_char;

pub mod ffi;
pub mod host;

/// Engine version string (`pm_wasmmod_version`).
pub fn version() -> &'static str {
    unsafe {
        let p = ffi::pm_wasmmod_version();
        if p.is_null() {
            return "";
        }
        CStr::from_ptr(p as *const c_char)
            .to_str()
            .unwrap_or("")
    }
}
