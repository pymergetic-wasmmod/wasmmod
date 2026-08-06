//! Thin wraps for `upy` / `dict` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_dict_copy`.
pub fn dict_copy(dict: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_dict_copy(dict) }
}

/// `pm_upy_dict_delete`.
pub fn dict_delete(dict: pm_upy_obj_t, key: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_dict_delete(dict, key) })
}

/// `pm_upy_dict_get`.
pub fn dict_get(dict: pm_upy_obj_t, key: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_dict_get(dict, key) }
}

/// `pm_upy_dict_store`.
pub fn dict_store(dict: pm_upy_obj_t, key: pm_upy_obj_t, val: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_dict_store(dict, key, val) })
}
