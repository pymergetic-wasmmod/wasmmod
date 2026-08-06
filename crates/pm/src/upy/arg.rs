//! Thin wraps for `upy` / `arg` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_arg_available`.
pub fn arg_available() -> i32 {
    unsafe { ffi::pm_upy_arg_available() }
}

/// `pm_upy_arg_parse`.
pub fn arg_parse(n_args: usize, args: *const pm_upy_obj_t, spec: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_arg_parse(n_args, args, spec) })
}
