//! Scheduler / pending-callback helpers.

use crate::check_status;
use crate::upy::ffi;
use crate::PmError;

/// Schedule a callable (`pm_upy_sched_schedule`).
///
/// `fun` / `arg` are host object pointers (same convention as the C API).
pub fn schedule(fun: *mut core::ffi::c_void, arg: *mut core::ffi::c_void) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_sched_schedule(fun, arg) })
}

/// Number of pending scheduled callbacks (`pm_upy_sched_num_pending`).
pub fn num_pending() -> i32 {
    unsafe { ffi::pm_upy_sched_num_pending() }
}

/// Lock the scheduler (`pm_upy_sched_lock`).
pub fn lock() {
    unsafe { ffi::pm_upy_sched_lock() }
}

/// Unlock the scheduler (`pm_upy_sched_unlock`).
pub fn unlock() {
    unsafe { ffi::pm_upy_sched_unlock() }
}

/// Run pending callbacks / exceptions (`pm_upy_handle_pending`).
pub fn handle_pending() -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_handle_pending() })
}

/// Wait/poll for events up to `ms` (`pm_upy_event_wait_ms`).
pub fn event_wait_ms(ms: u32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_event_wait_ms(ms) })
}
