//! Thin wraps for `wasmmod` / `host` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::wasmmod::ffi::{self, *};
use core::ffi::CStr;

/// `pm_wasmmod_host_call_export_i32`.
pub fn host_call_export_i32(pack: *const core::ffi::c_char, pack_len: usize, func: *const core::ffi::c_char, func_len: usize, nargs: u32, args: *const i32, out: *mut i32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_wasmmod_host_call_export_i32(pack, pack_len, func, func_len, nargs, args, out) })
}

/// `pm_wasmmod_host_clear_all`.
pub fn host_clear_all() -> () {
    unsafe { ffi::pm_wasmmod_host_clear_all() }
}

/// `pm_wasmmod_host_package_name`.
pub fn host_package_name() -> &'static str {
    unsafe {
        let p = ffi::pm_wasmmod_host_package_name();
        if p.is_null() { "" } else { CStr::from_ptr(p).to_str().unwrap_or("") }
    }
}

/// `pm_wasmmod_host_self_open`.
pub fn host_self_open() -> *mut pm_wasmmod_source_t {
    unsafe { ffi::pm_wasmmod_host_self_open() }
}

/// `pm_wasmmod_host_self_path`.
pub fn host_self_path(buf: *mut core::ffi::c_char, buflen: usize) -> pm_status_t {
    unsafe { ffi::pm_wasmmod_host_self_path(buf, buflen) }
}

/// `pm_wasmmod_host_set_self_image`.
pub fn host_set_self_image(buf: *const u8, len: u32, take_ownership: bool) -> () {
    unsafe { ffi::pm_wasmmod_host_set_self_image(buf, len, take_ownership) }
}

/// `pm_wasmmod_host_set_slot_c`.
pub fn host_set_slot_c(slot: i32, fn_: Option<unsafe extern "C" fn(arg: i32) -> i32>, userdata: *mut core::ffi::c_void) -> bool {
    unsafe { ffi::pm_wasmmod_host_set_slot_c(slot, fn_, userdata) }
}

/// `pm_wasmmod_host_slot_count`.
pub fn host_slot_count() -> usize {
    unsafe { ffi::pm_wasmmod_host_slot_count() }
}
