//! Thin wraps for `upy` / `buf` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_buf_get`.
pub fn buf_get(o: pm_upy_obj_t, ptr: *mut *const u8, len: *mut usize) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_buf_get(o, ptr, len) })
}
