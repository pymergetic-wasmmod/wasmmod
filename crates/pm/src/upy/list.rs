//! Thin wraps for `upy` / `list` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_list_append`.
pub fn list_append(list: pm_upy_obj_t, item: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_list_append(list, item) })
}

/// `pm_upy_list_remove`.
pub fn list_remove(list: pm_upy_obj_t, item: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_list_remove(list, item) })
}

/// `pm_upy_list_sort`.
pub fn list_sort(list: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_list_sort(list) })
}
