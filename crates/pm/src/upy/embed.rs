//! Thin wraps for `upy` / `embed` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_embed_deinit`.
pub fn embed_deinit() -> () {
    unsafe { ffi::pm_upy_embed_deinit() }
}

/// `pm_upy_embed_exec_mpy`.
pub fn embed_exec_mpy(mpy: *const core::ffi::c_void, len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_embed_exec_mpy(mpy, len) })
}

/// `pm_upy_embed_exec_str`.
pub fn embed_exec_str(src: *const core::ffi::c_char) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_embed_exec_str(src) })
}

/// `pm_upy_embed_init`.
pub fn embed_init(heap: *mut core::ffi::c_void, heap_len: usize, stack: *mut core::ffi::c_void, stack_len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_embed_init(heap, heap_len, stack, stack_len) })
}
