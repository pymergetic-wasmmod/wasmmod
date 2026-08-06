//! Thin wraps for `upy` / `ops` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_binary_op`.
pub fn binary_op(op: i32, lhs: pm_upy_obj_t, rhs: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_binary_op(op, lhs, rhs) }
}

/// `pm_upy_equal`.
pub fn equal(a: pm_upy_obj_t, b: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_equal(a, b) })
}

/// `pm_upy_get_type`.
pub fn get_type(o: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_get_type(o) }
}

/// `pm_upy_getiter`.
pub fn getiter(o: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_getiter(o) }
}

/// `pm_upy_is_subclass`.
pub fn is_subclass(obj: pm_upy_obj_t, classinfo: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_is_subclass(obj, classinfo) })
}

/// `pm_upy_iternext`.
pub fn iternext(o: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_iternext(o) }
}

/// `pm_upy_len`.
pub fn len(o: pm_upy_obj_t) -> usize {
    unsafe { ffi::pm_upy_len(o) }
}

/// `pm_upy_load_attr`.
pub fn load_attr(obj: pm_upy_obj_t, attr: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_load_attr(obj, attr) }
}

/// `pm_upy_load_global`.
pub fn load_global(name: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_load_global(name) }
}

/// `pm_upy_load_name`.
pub fn load_name(name: *const core::ffi::c_char) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_load_name(name) }
}

/// `pm_upy_store_attr`.
pub fn store_attr(obj: pm_upy_obj_t, attr: *const core::ffi::c_char, val: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_store_attr(obj, attr, val) })
}

/// `pm_upy_store_global`.
pub fn store_global(name: *const core::ffi::c_char, val: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_store_global(name, val) })
}

/// `pm_upy_store_name`.
pub fn store_name(name: *const core::ffi::c_char, val: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_store_name(name, val) })
}

/// `pm_upy_subscr`.
pub fn subscr(base: pm_upy_obj_t, index: pm_upy_obj_t, value: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_subscr(base, index, value) }
}

/// `pm_upy_unary_op`.
pub fn unary_op(op: i32, o: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_unary_op(op, o) }
}
