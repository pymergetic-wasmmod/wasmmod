//! Thin wraps for `upy` / `run` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_execute_bytecode`.
pub fn execute_bytecode(bc: *const u8, len: usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_execute_bytecode(bc, len) })
}

/// `pm_upy_make_closure`.
pub fn make_closure(raw_code: *mut core::ffi::c_void, n_closed: usize, closed: *mut pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_make_closure(raw_code, n_closed, closed) }
}

/// `pm_upy_make_function`.
pub fn make_function(raw_code: *mut core::ffi::c_void) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_make_function(raw_code) }
}

/// `pm_upy_parse_compile_execute`.
pub fn parse_compile_execute(src: *const core::ffi::c_char) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_parse_compile_execute(src) })
}

/// `pm_upy_run_script`.
pub fn run_script(path: *const core::ffi::c_char) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_run_script(path) })
}

/// `pm_upy_run_str`.
pub fn run_str(src: *const core::ffi::c_char) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_run_str(src) })
}
