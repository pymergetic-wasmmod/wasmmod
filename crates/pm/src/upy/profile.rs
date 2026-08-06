//! Thin wraps for `upy` / `profile` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_prof_instr_tick`.
pub fn prof_instr_tick() -> () {
    unsafe { ffi::pm_upy_prof_instr_tick() }
}

/// `pm_upy_profile_settrace`.
pub fn profile_settrace(cb: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_profile_settrace(cb) })
}
