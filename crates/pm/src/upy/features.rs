//! Feature / version probes (`pm_upy_features`, `pm_upy_has`, `pm_upy_version`).

use std::ffi::CStr;
use std::os::raw::c_char;

use crate::upy::ffi;

/// Bitmask of host µPy capabilities (`pm_upy_features`).
pub fn features() -> u32 {
    unsafe { ffi::pm_upy_features() }
}

/// Whether a feature bit is set (`pm_upy_has`).
pub fn has(feat: u32) -> bool {
    unsafe { ffi::pm_upy_has(feat) }
}

/// wasmmod / µPy version string (`pm_upy_version`).
pub fn version() -> &'static str {
    unsafe {
        let p = ffi::pm_upy_version();
        if p.is_null() {
            return "";
        }
        CStr::from_ptr(p as *const c_char)
            .to_str()
            .unwrap_or("")
    }
}
