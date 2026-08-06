//! Thin wraps for `upy` / `time` (`pm_upy_*`).
//!
//! Auto-generated from bindgen; prefer these over [`super::ffi`].

use crate::check_status;
use crate::PmError;
use crate::upy::ffi::{self, *};

/// `pm_upy_delay_ms`.
pub fn delay_ms(ms: u32) -> () {
    unsafe { ffi::pm_upy_delay_ms(ms) }
}

/// `pm_upy_delay_us`.
pub fn delay_us(us: u32) -> () {
    unsafe { ffi::pm_upy_delay_us(us) }
}

/// `pm_upy_sleep_us`.
pub fn sleep_us(us: u64) -> u32 {
    unsafe { ffi::pm_upy_sleep_us(us) }
}

/// `pm_upy_ticks_cpu`.
pub fn ticks_cpu() -> u32 {
    unsafe { ffi::pm_upy_ticks_cpu() }
}

/// `pm_upy_ticks_ms`.
pub fn ticks_ms() -> u32 {
    unsafe { ffi::pm_upy_ticks_ms() }
}

/// `pm_upy_ticks_us`.
pub fn ticks_us() -> u32 {
    unsafe { ffi::pm_upy_ticks_us() }
}

/// `pm_upy_time_localtime`.
pub fn time_localtime(seconds: i64, out: *mut i32) -> Result<(), PmError> {
    check_status(unsafe { ffi::pm_upy_time_localtime(seconds, out) })
}

/// `pm_upy_time_mktime`.
pub fn time_mktime(tm: *const i32) -> i64 {
    unsafe { ffi::pm_upy_time_mktime(tm) }
}

/// `pm_upy_time_ns`.
pub fn time_ns() -> u64 {
    unsafe { ffi::pm_upy_time_ns() }
}
