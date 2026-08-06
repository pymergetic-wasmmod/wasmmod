//! Thin wraps for `upy` / `await_` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_await`.
pub fn await_(self_h: u32, child_h: u32) -> u32 {
    unsafe { ffi::pm_upy_await(self_h, child_h) }
}

/// `pm_upy_gen_resume`.
pub fn gen_resume(gen: pm_upy_obj_t, send_val: pm_upy_obj_t, ret_out: *mut pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_gen_resume(gen, send_val, ret_out) })
}

/// `pm_upy_new_awaitable`.
pub fn new_awaitable(handle: u32) -> u32 {
    unsafe { ffi::pm_upy_new_awaitable(handle) }
}

/// `pm_upy_resume`.
pub fn resume(obj: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_resume(obj) })
}
