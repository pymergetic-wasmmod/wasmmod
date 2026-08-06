//! Thin wraps for `upy` / `asyncio` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_asyncio_available`.
pub fn asyncio_available() -> i32 {
    unsafe { ffi::pm_upy_asyncio_available() }
}

/// `pm_upy_asyncio_create_task`.
pub fn asyncio_create_task(coro: pm_upy_obj_t) -> pm_upy_obj_t {
    unsafe { ffi::pm_upy_asyncio_create_task(coro) }
}

/// `pm_upy_asyncio_run`.
pub fn asyncio_run(coro: pm_upy_obj_t) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_asyncio_run(coro) })
}
