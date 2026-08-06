//! Host self-image / package identity (`pm_wasmmod_host_*`).

use std::ffi::CStr;
use std::os::raw::c_char;

use crate::wasmmod::ffi;

/// Canonical first-party engine package name (`pm_wasmmod_host_package_name`).
pub fn package_name() -> &'static str {
    unsafe {
        let p = ffi::pm_wasmmod_host_package_name();
        cstr_or_empty(p)
    }
}

/// Open `wasmmod.source` for the running host (`pm_wasmmod_host_self_open`).
///
/// Caller closes with `pm_wasmmod_source_close`. Returns `None` if unresolved.
pub fn self_open() -> Option<*mut ffi::pm_wasmmod_source_t> {
    let p = unsafe { ffi::pm_wasmmod_host_self_open() };
    if p.is_null() {
        None
    } else {
        Some(p)
    }
}

fn cstr_or_empty(p: *const c_char) -> &'static str {
    if p.is_null() {
        return "";
    }
    unsafe { CStr::from_ptr(p) }.to_str().unwrap_or("")
}
