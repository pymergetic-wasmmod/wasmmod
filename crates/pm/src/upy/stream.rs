//! Thin wraps for `upy` / `stream` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_stream_available`.
pub fn stream_available() -> i32 {
    unsafe { ffi::pm_upy_stream_available() }
}

/// `pm_upy_stream_close`.
pub fn stream_close(stream: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_stream_close(stream) })
}

/// `pm_upy_stream_rw`.
pub fn stream_rw(stream: pm_upy_obj_t, buf: *mut core::ffi::c_void, len: usize, write: i32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_stream_rw(stream, buf, len, write) })
}

/// `pm_upy_stream_seek`.
pub fn stream_seek(stream: pm_upy_obj_t, off: i64, whence: i32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_stream_seek(stream, off, whence) })
}
