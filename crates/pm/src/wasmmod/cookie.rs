//! Thin wraps for `wasmmod` / `cookie` (`pm_wasmmod_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::wasmmod::ffi::{self, *};

/// `pm_wasmmod_mem_alloc`.
pub fn mem_alloc(size: u32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_wasmmod_mem_alloc(size) })
}

/// `pm_wasmmod_mem_alloc_copy`.
pub fn mem_alloc_copy(data: *const u8, len: u32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_wasmmod_mem_alloc_copy(data, len) })
}

/// `pm_wasmmod_mem_clear_all`.
pub fn mem_clear_all() -> () {
    unsafe { ffi::pm_wasmmod_mem_clear_all() }
}

/// `pm_wasmmod_mem_data`.
pub fn mem_data(cookie: i32, len_out: *mut u32) -> *const u8 {
    unsafe { ffi::pm_wasmmod_mem_data(cookie, len_out) }
}

/// `pm_wasmmod_mem_free`.
pub fn mem_free(cookie: i32) -> bool {
    unsafe { ffi::pm_wasmmod_mem_free(cookie) }
}

/// `pm_wasmmod_mem_len`.
pub fn mem_len(cookie: i32) -> u32 {
    unsafe { ffi::pm_wasmmod_mem_len(cookie) }
}

/// `pm_wasmmod_mem_set`.
pub fn mem_set(cookie: i32, data: *const u8, len: u32) -> bool {
    unsafe { ffi::pm_wasmmod_mem_set(cookie, data, len) }
}

/// `pm_wasmmod_mem_valid`.
pub fn mem_valid(cookie: i32) -> bool {
    unsafe { ffi::pm_wasmmod_mem_valid(cookie) }
}
