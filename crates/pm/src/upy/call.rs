//! Thin wraps for `upy` / `call` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_call_function_0`.
pub fn call_function_0(fun: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_call_function_0(fun) }
}

/// `pm_upy_call_function_1`.
pub fn call_function_1(fun: pm_upy_obj_t, arg: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_call_function_1(fun, arg) }
}

/// `pm_upy_call_function_n`.
pub fn call_function_n(fun: pm_upy_obj_t, n: usize, args: *mut pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_call_function_n(fun, n, args) }
}

/// `pm_upy_call_method`.
pub fn call_method(obj: pm_upy_obj_t, name: *const core::ffi::c_char, n: usize, args: *mut pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_call_method(obj, name, n, args) }
}

/// `pm_upy_fn_call_async`.
pub fn fn_call_async(fun: pm_upy_obj_t, n: usize, args: *mut pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_fn_call_async(fun, n, args) }
}

/// `pm_upy_fn_call_i32`.
pub fn fn_call_i32(fn_h: u32, a: i32, b: i32, out: *mut i32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_fn_call_i32(fn_h, a, b, out) })
}

/// `pm_upy_fn_resolve`.
pub fn fn_resolve(dotted: *const core::ffi::c_char) -> u32 {
    unsafe { ffi::pm_upy_fn_resolve(dotted) }
}
