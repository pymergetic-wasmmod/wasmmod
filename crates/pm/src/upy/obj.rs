//! Thin wraps for `upy` / `obj` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_obj_get_float`.
pub fn obj_get_float(o: pm_upy_obj_t) -> f64 {
    unsafe { ffi::pm_upy_obj_get_float(o) }
}

/// `pm_upy_obj_get_int`.
pub fn obj_get_int(o: pm_upy_obj_t) -> isize {
    unsafe { ffi::pm_upy_obj_get_int(o) }
}

/// `pm_upy_obj_get_ll`.
pub fn obj_get_ll(o: pm_upy_obj_t) -> i64 {
    unsafe { ffi::pm_upy_obj_get_ll(o) }
}

/// `pm_upy_obj_is_callable`.
pub fn obj_is_callable(o: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_obj_is_callable(o) })
}

/// `pm_upy_obj_is_true`.
pub fn obj_is_true(o: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_obj_is_true(o) })
}

/// `pm_upy_obj_new_bool`.
pub fn obj_new_bool(v: i32) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_bool(v) }
}

/// `pm_upy_obj_new_bound_meth`.
pub fn obj_new_bound_meth(meth: pm_upy_obj_t, self_: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_bound_meth(meth, self_) }
}

/// `pm_upy_obj_new_bytearray`.
pub fn obj_new_bytearray(n: usize, data: *const u8) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_bytearray(n, data) }
}

/// `pm_upy_obj_new_bytes`.
pub fn obj_new_bytes(b: *const u8, len: usize) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_bytes(b, len) }
}

/// `pm_upy_obj_new_cell`.
pub fn obj_new_cell(obj: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_cell(obj) }
}

/// `pm_upy_obj_new_closure`.
pub fn obj_new_closure(fun: pm_upy_obj_t, n: usize, closed: *mut pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_closure(fun, n, closed) }
}

/// `pm_upy_obj_new_complex`.
pub fn obj_new_complex(re: f64, im: f64) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_complex(re, im) }
}

/// `pm_upy_obj_new_dict`.
pub fn obj_new_dict() -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_dict() }
}

/// `pm_upy_obj_new_exception`.
pub fn obj_new_exception(type_name: *const core::ffi::c_char, msg: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_exception(type_name, msg) }
}

/// `pm_upy_obj_new_float`.
pub fn obj_new_float(v: f64) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_float(v) }
}

/// `pm_upy_obj_new_gen_wrap`.
pub fn obj_new_gen_wrap(fun: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_gen_wrap(fun) }
}

/// `pm_upy_obj_new_int`.
pub fn obj_new_int(i: isize) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_int(i) }
}

/// `pm_upy_obj_new_int_from_ll`.
pub fn obj_new_int_from_ll(i: i64) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_int_from_ll(i) }
}

/// `pm_upy_obj_new_list`.
pub fn obj_new_list(n: usize) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_list(n) }
}

/// `pm_upy_obj_new_memoryview`.
pub fn obj_new_memoryview(base: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_memoryview(base) }
}

/// `pm_upy_obj_new_module`.
pub fn obj_new_module(name: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_module(name) }
}

/// `pm_upy_obj_new_set`.
pub fn obj_new_set(n: usize) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_set(n) }
}

/// `pm_upy_obj_new_slice`.
pub fn obj_new_slice(start: pm_upy_obj_t, stop: pm_upy_obj_t, step: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_slice(start, stop, step) }
}

/// `pm_upy_obj_new_str`.
pub fn obj_new_str(s: *const core::ffi::c_char, len: usize) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_str(s, len) }
}

/// `pm_upy_obj_new_tuple`.
pub fn obj_new_tuple(n: usize) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_new_tuple(n) }
}

/// `pm_upy_obj_none`.
pub fn obj_none() -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_none() }
}

/// `pm_upy_obj_print`.
pub fn obj_print(o: pm_upy_obj_t) -> () {
    unsafe { ffi::pm_upy_obj_print(o) }
}

/// `pm_upy_obj_print_exception`.
pub fn obj_print_exception(exc: pm_upy_obj_t) -> () {
    unsafe { ffi::pm_upy_obj_print_exception(exc) }
}

/// `pm_upy_obj_str_get`.
pub fn obj_str_get(o: pm_upy_obj_t, data_out: *mut *const core::ffi::c_char, len_out: *mut usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_obj_str_get(o, data_out, len_out) })
}

/// `pm_upy_obj_to_str`.
pub fn obj_to_str(o: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_obj_to_str(o) }
}
