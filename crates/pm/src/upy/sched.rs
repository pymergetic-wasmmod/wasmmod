//! Thin wraps for `upy` / `sched` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_event_wait_ms`.
pub fn event_wait_ms(ms: u32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_event_wait_ms(ms) })
}

/// `pm_upy_handle_pending`.
pub fn handle_pending() -> i32 {
    unsafe { ffi::pm_upy_handle_pending() }
}

/// `pm_upy_sched_exception`.
pub fn sched_exception(exc: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_sched_exception(exc) })
}

/// `pm_upy_sched_keyboard_interrupt`.
pub fn sched_keyboard_interrupt() -> () {
    unsafe { ffi::pm_upy_sched_keyboard_interrupt() }
}

/// `pm_upy_sched_lock`.
pub fn sched_lock() -> () {
    unsafe { ffi::pm_upy_sched_lock() }
}

/// `pm_upy_sched_num_pending`.
pub fn sched_num_pending() -> i32 {
    unsafe { ffi::pm_upy_sched_num_pending() }
}

/// `pm_upy_sched_schedule`.
pub fn sched_schedule(fun: *mut core::ffi::c_void, arg: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_sched_schedule(fun, arg) })
}

/// `pm_upy_sched_unlock`.
pub fn sched_unlock() -> () {
    unsafe { ffi::pm_upy_sched_unlock() }
}
